#!/usr/bin/env python3
"""
apply_blackhole_patch.py
========================
Patches NS-3.42 AODV source to add a BlackholeNode attribute.

Run from the ns3 root directory AFTER restoring the .bak originals:
    cp src/aodv/model/aodv-routing-protocol.cc.bak src/aodv/model/aodv-routing-protocol.cc
    cp src/aodv/model/aodv-routing-protocol.h.bak  src/aodv/model/aodv-routing-protocol.h
    python3 apply_blackhole_patch.py
    ./ns3 build
"""

import sys
import os
import shutil

CC = "src/aodv/model/aodv-routing-protocol.cc"
H  = "src/aodv/model/aodv-routing-protocol.h"

for f in [CC, H]:
    if not os.path.exists(f):
        print(f"ERROR: {f} not found. Run from the ns3 root directory.")
        sys.exit(1)

for f in [CC, H]:
    bak = f + ".bak"
    if not os.path.exists(bak):
        shutil.copy2(f, bak)
        print(f"  Backed up {f} -> {bak}")
    else:
        print(f"  Backup already exists: {bak}")

def patch_file(path, old, new, description):
    with open(path, "r") as fh:
        content = fh.read()
    if old not in content:
        # Check if already applied
        already = {
            "Header: add m_blackhole member":        "m_blackhole" in content,
            "CC: register BlackholeNode attribute":  "BlackholeNode" in content,
            "CC: RouteInput blackhole drop logic":   "BLACKHOLE DROP" in content,
            "CC: RecvRequest fake RREP injection":   "BLACKHOLE: send fake" in content,
        }
        for key, applied in already.items():
            if description.startswith(key.split()[0]) and applied:
                print(f"  SKIP  [{description}] — already applied")
                return
        print(f"  ERROR [{description}] — anchor not found in {path}")
        print(f"  Looking for: {old[:120]!r}")
        sys.exit(1)
    count = content.count(old)
    if count > 1:
        print(f"  ERROR [{description}] — anchor matches {count} times, must be unique")
        sys.exit(1)
    content = content.replace(old, new, 1)
    with open(path, "w") as fh:
        fh.write(content)
    print(f"  OK    [{description}]")

# ==========================================================
# PATCH 1 - Header: add m_blackhole member
# ==========================================================
patch_file(
    H,
    "    /// Provides uniform random variables.\n"
    "    Ptr<UniformRandomVariable> m_uniformRandomVariable;",
    "    /// Provides uniform random variables.\n"
    "    Ptr<UniformRandomVariable> m_uniformRandomVariable;\n"
    "    bool m_blackhole; ///< When true, node acts as a blackhole attacker.",
    "Header: add m_blackhole member"
)

# ==========================================================
# PATCH 2 - GetTypeId(): register BlackholeNode attribute
# ==========================================================
patch_file(
    CC,
    '                          MakePointerChecker<UniformRandomVariable>());\n',
    '                          MakePointerChecker<UniformRandomVariable>())\n'
    '    .AddAttribute ("BlackholeNode",\n'
    '                   "When true, node acts as a blackhole attacker (default: false).",\n'
    '                   BooleanValue (false),\n'
    '                   MakeBooleanAccessor (&RoutingProtocol::m_blackhole),\n'
    '                   MakeBooleanChecker ());\n',
    "CC: register BlackholeNode attribute in GetTypeId()"
)

# ==================================================================
# PATCH 3 - RouteInput(): drop forwarded packets when blackhole
# Anchor: exact text from line 507 in your file (no spaces in macro)
# ==================================================================
ROUTE_INPUT_LOG = (
    '    NS_LOG_FUNCTION(this << p->GetUid() << header.GetDestination()'
    ' << idev->GetAddress());\n'
)

BLACKHOLE_DROP = (
    '    /* --- BLACKHOLE DROP (only when m_blackhole == true) --- */\n'
    '    if (m_blackhole)\n'
    '    {\n'
    '        Ipv4Address dst = header.GetDestination();\n'
    '        bool isLocal = false;\n'
    '        for (uint32_t ii = 0; ii < m_ipv4->GetNInterfaces(); ++ii)\n'
    '            for (uint32_t jj = 0; jj < m_ipv4->GetNAddresses(ii); ++jj)\n'
    '                if (m_ipv4->GetAddress(ii, jj).GetLocal() == dst)\n'
    '                    isLocal = true;\n'
    '        bool isAodvCtrl = false;\n'
    '        if (header.GetProtocol() == 17)\n'
    '        {\n'
    '            Ptr<Packet> tmp = p->Copy();\n'
    '            UdpHeader uh;\n'
    '            if (tmp->GetSize() >= uh.GetSerializedSize())\n'
    '            {\n'
    '                tmp->PeekHeader(uh);\n'
    '                if (uh.GetSourcePort()      == AODV_PORT ||\n'
    '                    uh.GetDestinationPort() == AODV_PORT)\n'
    '                    isAodvCtrl = true;\n'
    '            }\n'
    '        }\n'
    '        if (!isLocal && !isAodvCtrl)\n'
    '        {\n'
    '            NS_LOG_DEBUG("BLACKHOLE drop " << header.GetSource()\n'
    '                         << " -> " << dst);\n'
    '            return true;\n'
    '        }\n'
    '    }\n'
    '    /* --- END BLACKHOLE DROP --- */\n'
)

patch_file(
    CC,
    ROUTE_INPUT_LOG,
    BLACKHOLE_DROP + ROUTE_INPUT_LOG,
    "CC: RouteInput blackhole drop logic"
)

# ==========================================================
# PATCH 4 - RecvRequest(): send fake RREP when blackhole
# ==========================================================
RECV_REQUEST_ANCHOR = (
    'RoutingProtocol::RecvRequest(Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src)\n'
    '{\n'
    '    NS_LOG_FUNCTION(this);\n'
)

FAKE_RREP = (
    'RoutingProtocol::RecvRequest(Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src)\n'
    '{\n'
    '    /* --- BLACKHOLE: send fake RREP (only when m_blackhole) --- */\n'
    '    if (m_blackhole)\n'
    '    {\n'
    '        Ptr<Packet> pCopy = p->Copy();\n'
    '        RreqHeader rreqHdr;\n'
    '        pCopy->RemoveHeader(rreqHdr);\n'
    '        RrepHeader fakeRrep;\n'
    '        fakeRrep.SetDst      (rreqHdr.GetDst());\n'
    '        fakeRrep.SetDstSeqno (rreqHdr.GetDstSeqno() + 100);\n'
    '        fakeRrep.SetOrigin   (rreqHdr.GetOrigin());\n'
    '        fakeRrep.SetHopCount (0);\n'
    '        fakeRrep.SetLifeTime (MilliSeconds(10000));\n'
    '        Ptr<Packet> fakeP = Create<Packet>();\n'
    '        fakeP->AddHeader(fakeRrep);\n'
    '        TypeHeader th(AODVTYPE_RREP);\n'
    '        fakeP->AddHeader(th);\n'
    '        for (auto it = m_socketAddresses.begin();\n'
    '             it != m_socketAddresses.end(); ++it)\n'
    '        {\n'
    '            it->first->SendTo(fakeP->Copy(), 0,\n'
    '                InetSocketAddress(rreqHdr.GetOrigin(), AODV_PORT));\n'
    '        }\n'
    '        NS_LOG_DEBUG("BLACKHOLE fake RREP for dst " << rreqHdr.GetDst());\n'
    '        return;\n'
    '    }\n'
    '    /* --- END BLACKHOLE RREP --- */\n'
    '    NS_LOG_FUNCTION(this);\n'
)

patch_file(
    CC,
    RECV_REQUEST_ANCHOR,
    FAKE_RREP,
    "CC: RecvRequest fake RREP injection"
)

print()
print("=" * 56)
print("  Patch applied successfully.")
print("  Now rebuild:")
print("    ./ns3 build")
print("=" * 56)