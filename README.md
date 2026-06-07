# Black hole Attack on Emergency Rescue MANET
### NS-3 Simulation | AODV | Post-Earthquake Disaster Zone

A Master's thesis simulation project that quantifies the impact of black hole attacks on an AODV-based Mobile Ad Hoc Network (MANET) modeled after a post-earthquake emergency rescue scenario.

---

## Overview

When an earthquake destroys fixed communication infrastructure, rescue teams rely on MANETs to coordinate. This project simulates what happens when one or more nodes in that network are compromised and execute a black hole attack: intercepting all traffic routed through them and silently discarding every packet.

Three scenarios are evaluated:

| Scenario | Description                        | Attacker Nodes     |
|----------|------------------------------------|--------------------|
| S1       | Normal operation, no attack        | None               |
| S2       | Single black hole node             | Node N5            |
| S3       | Three independent black hole nodes | Nodes N5, N12, N18 |

Each scenario runs across 10 independent seeds and reports 7 performance metrics.

---

## Results Summary

| Metric                   | S1 Normal | S2 (1 BH) | S3 (3 BH) |
|--------------------------|-----------|-----------|-----------|
| PDR (%)                  | 84.35     | 60.15     | 56.25     |
| PLR (%)                  | 15.65     | 39.85     | 43.75     |
| Avg E2E Delay (ms)       | 24.89     | 28.22     | 71.73     | 
| Throughput (kbps)        | 7.29      | 5.20      | 4.86      |
| Route Discovery Time (s) | 1.21      | 6.03      | 4.46      |
| BH Packets Dropped       | 0         | 54.7      | 60.4      |
| Routing Overhead         | 3.36      | 5.79      | 4.15      |

**Key finding:** A single black hole node (3.3% of the network) drops PDR by 24.2 percentage points. Adding two more attackers produces only 3.9 pp additional degradation due to traffic saturation.

---

## Scenario Description

- **Area:** 600 x 600 m (collapsed urban block)
- **Nodes:** 30 total
  - 20 rescue workers (N0-N19) moving at 1-3 m/s
  - 10 ambulances (N20-N29) moving at 5-15 m/s
- **Source:** Node N0 (Rescue Commander)
- **Destination:** Node N29 (Medical HQ)
- **Traffic:** 200 UDP packets x 512 bytes every 0.5 seconds
- **Protocol:** AODV (RFC 3561)
- **Channel:** IEEE 802.11b, LogDistance propagation, 20 dBm TX, ~250 m range

---

## Project Structure

```
.
├── manet-rescue-normal.cc      # S1 simulation (no attack), 10 seeds
├── manet-rescue-attack.cc      # S2 and S3 simulation (black hole attack)
├── apply_blackhole_patch.py    # Patches NS-3 AODV source for attack behavior
└── README.md
```

---

## Requirements

- Windows 11 with Docker Desktop (WSL2 backend enabled)
- OR any Linux machine with NS-3 v3.42

---

## Setup

### 1. Install Docker Desktop on Windows

Download from https://www.docker.com/products/docker-desktop and enable WSL2 during installation.

### 2. Create the Ubuntu container

```powershell
# In PowerShell, navigate to this project folder
cd "C:\path\to\this\project"

# Create container with shared folder
docker run -it --name ns3sim -v "${PWD}:/shared" ubuntu:22.04 bash
```

### 3. Install dependencies inside the container

```bash
apt update && apt upgrade -y && apt install -y \
  g++ python3 python3-pip cmake ninja-build git \
  libboost-all-dev tcpdump
```

### 4. Clone and build NS-3 v3.42

```bash
git clone https://gitlab.com/nsnam/ns-3-dev.git --branch ns-3.42 ns3
cd ns3
./ns3 configure --enable-examples --enable-tests
./ns3 build
# Takes 10-20 minutes
```

### 5. Restart after shutdown

The container and NS-3 build persist between restarts:

```powershell
docker start -ai ns3sim
```

```bash
cd ns3
```

---

## Running the Simulations

### Copy files into NS-3

```bash
cp /shared/manet-rescue-normal.cc scratch/
cp /shared/manet-rescue-attack.cc scratch/
cp /shared/apply_blackhole_patch.py .
```

### S1 - Normal simulation (no patch needed)

```bash
./ns3 run scratch/manet-rescue-normal
```

### Apply the black hole patch before S2 and S3

```bash
# Restore originals first (always do this before patching)
cp src/aodv/model/aodv-routing-protocol.cc.bak src/aodv/model/aodv-routing-protocol.cc
cp src/aodv/model/aodv-routing-protocol.h.bak src/aodv/model/aodv-routing-protocol.h

# Apply patch
python3 apply_blackhole_patch.py

# Rebuild
./ns3 build
```

Expected output from the patch script:
```
OK    [Header: add m_blackhole member]
OK    [CC: register BlackholeNode attribute in GetTypeId()]
OK    [CC: RouteInput black hole drop logic]
OK    [CC: RecvRequest fake RREP injection]
```

### S2 - Single black hole node (Node N5)

```bash
./ns3 run scratch/manet-rescue-attack
```

### S3 - Three black hole nodes (Nodes N5, N12, N18)

```bash
./ns3 run "scratch/manet-rescue-attack --nBlackhole=3"
```

---

## How the Black hole Attack Works

The patch adds a `BlackholeNode` boolean attribute to the NS-3 AODV module. It defaults to `false` so the normal simulation is completely unaffected.

When set to `true` on a node, two behaviors are activated:

**1. Fake RREP injection**
When the attacker receives an RREQ, it immediately replies with a fake RREP claiming:
- Destination sequence number = legitimate seqno + 100 (appears most fresh)
- Hop count = 0 (appears as direct one-hop route)

Since AODV always selects the route with the highest sequence number and fewest hops, every other node routes traffic through the attacker.

**2. Silent packet drop**
All data packets forwarded through the attacker are silently discarded. No RERR message is generated, so the source cannot immediately detect the failure.

AODV control traffic on UDP port 654 (hello messages, RREQs, RREPs, RERRs) is never dropped, so the attacker keeps participating in route discovery and retaining its preferred position.

---

## Verify Patch Status

```bash
grep -c "m_blackhole" src/aodv/model/aodv-routing-protocol.cc
# 4 = patch is active
# 0 = patch is not applied
```

---

## Performance Metrics

| Metric               | Formula                                               |
|----------------------|-------------------------------------------------------|
| PDR                  | Rx / Tx x 100%                                        |
| PLR                  | (Tx - Rx) / Tx x 100%                                 |
| E2E Delay            | delaySum / Rx (ms)                                    |
| Throughput           | rxBytes x 8 / 100s (kbps)                             |
| Route Discovery Time | timeFirstRxPacket - 15.0 (s)                          |
| BH Drops             | Lost Packets after BH - Originally lost packets (pkts)|
| Routing Overhead     | AODV control packets (port 654) / Rx                  |

All metrics are extracted from NS-3 FlowMonitor output at the end of each run. Results are averaged across 10 seeds.

---

## Simulator

- **NS-3 version:** 3.42
- **OS:** Ubuntu 22.04 (inside Docker on Windows 11)
- **AODV:** Native NS-3 module, RFC 3561 compliant
- **Seeds:** 1 through 10 (same seeds used for all three scenarios)

---

## References

- Al-Shurman, M., Yoo, S-M., Park, S. (2004). Black hole attack in mobile ad hoc networks. ACMSE'04.
- Perkins, C. E., Belding-Royer, E., Das, S. (2003). AODV routing. IETF RFC 3561.
- Patel, N.J.K., Tripathi, K. (2017). Analysis of black hole attack in MANET using NS3.26. IJRITCC 5(5).
- Malik, T.S. et al. (2022). Comparison of blackhole and wormhole attacks in cloud MANET. Security and Communication Networks.

---

## License

This project was developed as part of a Master's thesis in Network Security. The simulation scripts and patch are provided for academic and research purposes.
