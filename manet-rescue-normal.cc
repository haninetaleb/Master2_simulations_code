/* =============================================================
 *  EMERGENCY RESCUE MANET — NORMAL SIMULATION (NO ATTACK)
 *  Post-earthquake disaster zone  |  30 nodes  |  AODV
 * =============================================================
 *
 *  BUILD & RUN:
 *    cp /shared/manet-rescue-normal.cc scratch/
 *    ./ns3 run scratch/manet-rescue-normal

 * ============================================================= */

#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;
NS_LOG_COMPONENT_DEFINE("ManetRescueNormal");

struct RunResult
{
    uint32_t seed;
    double pdr, plr, delay, throughput, rdt;
    uint32_t txPkts, rxPkts, lostPkts;
    double routingOverhead;
};

RunResult RunSimulation(uint32_t seed)
{
    // --- parameters -----------------------------------
    const uint32_t nRescue = 20;
    const uint32_t nAmbulance = 10;
    const double areaSize = 600.0;
    const double simTime = 120.0;
    const uint16_t port = 9;
    const uint32_t srcNode = 0;
    const uint32_t dstNode = 29;
    const uint32_t maxPkts = 200;
    const double interval = 0.5;
    const uint32_t pktSize = 512;
    const double trafficStart = 15.0;
    const double trafficStop = trafficStart + maxPkts * interval; // 115s

    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(1);

    // --- nodes -----------------------------------
    NodeContainer rescue, ambulance, all;
    rescue.Create(nRescue);
    ambulance.Create(nAmbulance);
    all.Add(rescue);
    all.Add(ambulance);

    // --- WiFi 802.11b ad-hoc -----------------------------------
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("DsssRate2Mbps"),
                                 "ControlMode", StringValue("DsssRate1Mbps"));

    YansWifiChannelHelper ch;
    ch.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    ch.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                          "Exponent", DoubleValue(2.5),
                          "ReferenceLoss", DoubleValue(46.6777),
                          "ReferenceDistance", DoubleValue(1.0));

    YansWifiPhyHelper phy;
    phy.SetChannel(ch.Create());
    phy.Set("TxPowerStart", DoubleValue(20.0));
    phy.Set("TxPowerEnd", DoubleValue(20.0));

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(phy, mac, all);

    // --- mobility -----------------------------------
    std::ostringstream oss;
    oss << "ns3::UniformRandomVariable[Min=0|Max=" << (int)areaSize << "]";
    std::string rect = oss.str();

    // Rescue workers: 1-3 m/s pedestrians
    MobilityHelper mobRescue;
    mobRescue.SetPositionAllocator(
        "ns3::RandomRectanglePositionAllocator",
        "X", StringValue(rect), "Y", StringValue(rect));
    mobRescue.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                               "Speed", StringValue("ns3::UniformRandomVariable[Min=1|Max=3]"),
                               "Pause", StringValue("ns3::ConstantRandomVariable[Constant=2.0]"),
                               "PositionAllocator",
                               PointerValue(CreateObjectWithAttributes<RandomRectanglePositionAllocator>(
                                   "X", StringValue(rect), "Y", StringValue(rect))));
    mobRescue.Install(rescue);

    // Ambulances: 5-15 m/s vehicles
    MobilityHelper mobAmbu;
    mobAmbu.SetPositionAllocator(
        "ns3::RandomRectanglePositionAllocator",
        "X", StringValue(rect), "Y", StringValue(rect));
    mobAmbu.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                             "Speed", StringValue("ns3::UniformRandomVariable[Min=5|Max=15]"),
                             "Pause", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"),
                             "PositionAllocator",
                             PointerValue(CreateObjectWithAttributes<RandomRectanglePositionAllocator>(
                                 "X", StringValue(rect), "Y", StringValue(rect))));
    mobAmbu.Install(ambulance);

    // --- AODV + internet stack -----------------------------------
    AodvHelper aodv;
    InternetStackHelper inet;
    inet.SetRoutingHelper(aodv);
    inet.Install(all);

    Ipv4AddressHelper ipAddr;
    ipAddr.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifaces = ipAddr.Assign(devices);

    // --- applications -----------------------------------
    UdpEchoServerHelper echoServer(port);
    ApplicationContainer serverApp = echoServer.Install(all.Get(dstNode));
    serverApp.Start(Seconds(1.0));
    serverApp.Stop(Seconds(simTime));

    UdpEchoClientHelper echoClient(ifaces.GetAddress(dstNode), port);
    echoClient.SetAttribute("MaxPackets", UintegerValue(maxPkts));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(interval)));
    echoClient.SetAttribute("PacketSize", UintegerValue(pktSize));
    ApplicationContainer clientApp = echoClient.Install(all.Get(srcNode));
    clientApp.Start(Seconds(trafficStart));
    clientApp.Stop(Seconds(trafficStop));

    // --- flow monitor -----------------------------------
    FlowMonitorHelper fmh;
    Ptr<FlowMonitor> fm = fmh.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // --- collect results -----------------------------------
    fm->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> clf =
        DynamicCast<Ipv4FlowClassifier>(fmh.GetClassifier());

    RunResult r;
    r.seed = seed;
    r.pdr = r.plr = r.delay = r.throughput = r.rdt = 0.0;
    r.txPkts = r.rxPkts = r.lostPkts = 0;
    r.routingOverhead = 0.0;

    Ipv4Address srcAddr = ifaces.GetAddress(srcNode);
    Ipv4Address dstAddr = ifaces.GetAddress(dstNode);

    // Count AODV control packets (UDP port 654) across ALL flows
    uint32_t ctrlPkts = 0;
    for (auto &fs : fm->GetFlowStats())
    {
        auto t = clf->FindFlow(fs.first);
        if (t.protocol == 17 &&
            (t.sourcePort == 654 || t.destinationPort == 654))
        {
            ctrlPkts += fs.second.txPackets;
        }
    }

    // Find the data flow src->dst
    for (auto &fs : fm->GetFlowStats())
    {
        auto t = clf->FindFlow(fs.first);
        if (t.sourceAddress == srcAddr &&
            t.destinationAddress == dstAddr &&
            t.protocol == 17)
        {
            r.txPkts = fs.second.txPackets;
            r.rxPkts = fs.second.rxPackets;
            r.lostPkts = (r.txPkts > r.rxPkts) ? (r.txPkts - r.rxPkts) : 0;

            double duration = maxPkts * interval; // 100s
            r.throughput = (r.rxPkts > 0)
                               ? fs.second.rxBytes * 8.0 / duration / 1000.0
                               : 0.0;
            if (r.rxPkts > 0)
                r.delay = fs.second.delaySum.GetSeconds() / (double)r.rxPkts * 1000.0;
            r.rdt = (fs.second.rxPackets > 0)
                        ? fs.second.timeFirstRxPacket.GetSeconds() - trafficStart
                        : -1.0;
            break;
        }
    }

    if (r.txPkts > 0)
    {
        r.pdr = std::min((double)r.rxPkts / r.txPkts * 100.0, 100.0);
        r.plr = std::min((double)r.lostPkts / r.txPkts * 100.0, 100.0);
    }

    // Routing Overhead = control packets / received data packets
    // Measures how many AODV packets were needed per delivered data packet.
    r.routingOverhead = (r.rxPkts > 0)
                            ? (double)ctrlPkts / (double)r.rxPkts
                            : (double)ctrlPkts;

    // Diagnostic
    Vector sp = all.Get(srcNode)->GetObject<MobilityModel>()->GetPosition();
    Vector dp = all.Get(dstNode)->GetObject<MobilityModel>()->GetPosition();
    double dist = std::sqrt(std::pow(sp.x - dp.x, 2) + std::pow(sp.y - dp.y, 2));
    NS_LOG_UNCOND("    [diag] src=(" << (int)sp.x << "," << (int)sp.y
                                     << ") dst=(" << (int)dp.x << "," << (int)dp.y
                                     << ") dist=" << (int)dist << "m"
                                     << "  tx=" << r.txPkts << " rx=" << r.rxPkts
                                     << " PDR=" << r.pdr << "%"
                                     << " RO=" << r.routingOverhead);

    Simulator::Destroy();
    return r;
}

// --- main -----------------------------------
int main(int argc, char *argv[])
{
    CommandLine cmd(__FILE__);
    cmd.Parse(argc, argv);

    const uint32_t SEEDS[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const int NRUNS = 10;

    NS_LOG_UNCOND("\n======================================================");
    NS_LOG_UNCOND("  EMERGENCY RESCUE MANET — NORMAL (10 RUNS)");
    NS_LOG_UNCOND("======================================================");
    NS_LOG_UNCOND("  Context  : Post-earthquake disaster zone");
    NS_LOG_UNCOND("  Area     : 600 x 600 m");
    NS_LOG_UNCOND("  Nodes    : 30  (20 rescue workers + 10 ambulances)");
    NS_LOG_UNCOND("  Protocol : AODV  |  TX Range: ~250 m");
    NS_LOG_UNCOND("  Traffic  : 200 pkts x 512 B @ 0.5s interval");
    NS_LOG_UNCOND("  Duration : 120 s  |  Attack: NONE");
    NS_LOG_UNCOND("======================================================\n");

    std::vector<RunResult> results;

    for (int i = 0; i < NRUNS; i++)
    {
        uint32_t seed = SEEDS[i];
        NS_LOG_UNCOND("  Running seed " << seed << " / "
                                        << SEEDS[NRUNS - 1] << " ...");
        RunResult r = RunSimulation(seed);
        results.push_back(r);
        NS_LOG_UNCOND("    Seed " << seed
                                  << "  PDR=" << r.pdr << "%"
                                  << "  PLR=" << r.plr << "%"
                                  << "  Delay=" << r.delay << "ms"
                                  << "  Thput=" << r.throughput << "kbps"
                                  << "  RDT=" << r.rdt << "s"
                                  << "  RO=" << r.routingOverhead);
    }

    NS_LOG_UNCOND("\n=====================================================================================");
    NS_LOG_UNCOND("  PER-RUN RESULTS — NORMAL SIMULATION");
    NS_LOG_UNCOND("=====================================================================================");
    NS_LOG_UNCOND("  Seed | Tx  | Rx  | Lost | PDR(%) | PLR(%) | "
                  "Delay(ms) | Thput(kbps) | RDT(s) | RO");
    NS_LOG_UNCOND("  -----|-----|-----|------|--------|--------|"
                  "-----------|------------|--------|------");

    double sumPdr = 0, sumPlr = 0, sumDelay = 0, sumTp = 0, sumRdt = 0, sumRO = 0;
    double sumTx = 0, sumRx = 0, sumLost = 0;
    int validRdt = 0;
    for (auto &r : results)
    {
        char buf[260];
        snprintf(buf, sizeof(buf),
                 "  %4u | %3u | %3u | %4u | %6.2f | %6.2f | %9.3f | %10.4f | %6.4f | %.2f",
                 r.seed, r.txPkts, r.rxPkts, r.lostPkts,
                 r.pdr, r.plr, r.delay, r.throughput, r.rdt, r.routingOverhead);
        NS_LOG_UNCOND(buf);
        sumTx += r.txPkts;
        sumRx += r.rxPkts;
        sumLost += r.lostPkts;
        sumPdr += r.pdr;
        sumPlr += r.plr;
        sumDelay += r.delay;
        sumTp += r.throughput;
        sumRO += r.routingOverhead;
        if (r.rdt >= 0.0)
        {
            sumRdt += r.rdt;
            validRdt++;
        }
    }

    double n = (double)NRUNS;

    // --- Mean row -----------------------------------
    NS_LOG_UNCOND("  -----|-----|-----|------|--------|--------|"
                  "-----------|------------|--------|------");
    {
        char buf[260];
        snprintf(buf, sizeof(buf),
                 "  %4s | %3.0f | %3.0f | %4.0f | %6.2f | %6.2f | %9.3f | %10.4f | %6.4f | %.2f",
                 "Mean",
                 sumTx / n, sumRx / n, sumLost / n,
                 sumPdr / n, sumPlr / n, sumDelay / n, sumTp / n,
                 (validRdt > 0 ? sumRdt / validRdt : 0.0),
                 sumRO / n);
        NS_LOG_UNCOND(buf);
    }

    NS_LOG_UNCOND("\n======================================================");
    NS_LOG_UNCOND("  AVERAGE RESULTS OVER 10 RUNS — NORMAL SIMULATION");
    NS_LOG_UNCOND("======================================================");
    NS_LOG_UNCOND("  Metric                       | Average Value");
    NS_LOG_UNCOND("  -----------------------------|----------------");
    NS_LOG_UNCOND("  Packet Delivery Ratio (PDR)  | " << sumPdr / n << " %");
    NS_LOG_UNCOND("  Packet Loss Rate (PLR)       | " << sumPlr / n << " %");
    NS_LOG_UNCOND("  Avg End-to-End Delay         | " << sumDelay / n << " ms");
    NS_LOG_UNCOND("  Throughput                   | " << sumTp / n << " kbps");
    NS_LOG_UNCOND("  Route Discovery Time         | "
                  << (validRdt > 0 ? sumRdt / validRdt : 0.0) << " s");
    NS_LOG_UNCOND("  Routing Overhead (ctrl/rx)   | " << sumRO / n);
    NS_LOG_UNCOND("======================================================\n");

    return 0;
}