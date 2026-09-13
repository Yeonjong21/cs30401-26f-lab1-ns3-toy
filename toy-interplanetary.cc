/* Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <fstream>
#include <iomanip>
#include <list>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ToyInterplanetary");

class TcpRelay : public Application
{
  public:
    static TypeId GetTypeId()
    {
        static TypeId tid = TypeId("TcpRelay")
                                .SetParent<Application>()
                                .SetGroupName("Applications")
                                .AddConstructor<TcpRelay>();
        return tid;
    }

    void Setup(uint16_t listenPort, Address nextHop)
    {
        m_listenPort = listenPort;
        m_nextHop = nextHop;
    }

    uint64_t GetBuffered() const
    {
        return m_buffered;
    }

    uint64_t GetPeakBuffered() const
    {
        return m_peakBuffered;
    }

  private:
    void StartApplication() override
    {
        m_listen = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
        m_listen->Bind(InetSocketAddress(Ipv4Address::GetAny(), m_listenPort));
        m_listen->Listen();
        m_listen->SetAcceptCallback(
            MakeCallback(&TcpRelay::OnAcceptRequest, this),
            MakeCallback(&TcpRelay::OnAccept, this));
    }

    void StopApplication() override
    {
        if (m_listen)
        {
            m_listen->Close();
        }
        if (m_up)
        {
            m_up->Close();
        }
        if (m_down)
        {
            m_down->Close();
        }
    }

    bool OnAcceptRequest(Ptr<Socket>, const Address&)
    {
        return true;
    }

    void OnAccept(Ptr<Socket> s, const Address&)
    {
        m_up = s;
        m_up->SetRecvCallback(MakeCallback(&TcpRelay::OnUpstreamRecv, this));
        m_up->SetCloseCallbacks(MakeCallback(&TcpRelay::OnUpstreamClose, this),
                                MakeCallback(&TcpRelay::OnUpstreamClose, this));

        m_down = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
        m_down->Bind();
        m_down->Connect(m_nextHop);
        m_down->SetConnectCallback(MakeCallback(&TcpRelay::OnDownstreamUp, this),
                                   MakeCallback(&TcpRelay::OnDownstreamFail, this));
        m_down->SetSendCallback(MakeCallback(&TcpRelay::OnDownstreamSendReady, this));
    }

    void OnDownstreamUp(Ptr<Socket>)
    {
        m_downReady = true;
        Pump();
    }

    void OnDownstreamFail(Ptr<Socket>)
    {
        NS_FATAL_ERROR("relay: downstream connection failed");
    }

    void OnDownstreamSendReady(Ptr<Socket>, uint32_t)
    {
        Pump();
    }

    void OnUpstreamRecv(Ptr<Socket> s)
    {
        Ptr<Packet> p;
        while ((p = s->Recv()))
        {
            if (p->GetSize() == 0)
            {
                break;
            }
            m_buffered += p->GetSize();
            m_queue.push_back(p);
        }
        m_peakBuffered = std::max(m_peakBuffered, m_buffered);
        Pump();
    }

    void OnUpstreamClose(Ptr<Socket>)
    {
        m_upClosed = true;
        Pump();
    }

    void Pump()
    {
        if (!m_downReady)
        {
            return;
        }
        while (!m_queue.empty())
        {
            Ptr<Packet> head = m_queue.front();
            uint32_t avail = m_down->GetTxAvailable();
            if (avail == 0)
            {
                return;
            }
            if (avail >= head->GetSize())
            {
                int sent = m_down->Send(head);
                if (sent <= 0)
                {
                    return;
                }
                m_buffered -= head->GetSize();
                m_queue.pop_front();
            }
            else
            {
                Ptr<Packet> frag = head->CreateFragment(0, avail);
                int sent = m_down->Send(frag);
                if (sent <= 0)
                {
                    return;
                }
                m_queue.pop_front();
                m_queue.push_front(head->CreateFragment(avail, head->GetSize() - avail));
                m_buffered -= avail;
            }
        }
        if (m_upClosed && m_queue.empty() && m_down)
        {
            m_down->Close();
        }
    }

    uint16_t m_listenPort{0};
    Address m_nextHop;
    Ptr<Socket> m_listen;
    Ptr<Socket> m_up;
    Ptr<Socket> m_down;
    std::list<Ptr<Packet>> m_queue;
    uint64_t m_buffered{0};
    uint64_t m_peakBuffered{0};
    bool m_downReady{false};
    bool m_upClosed{false};
};

// ===========================================================================
// Measurement
// ===========================================================================
static uint64_t g_rxBytes = 0;
static Time g_lastRx = Seconds(0);
static Time g_txStart = Seconds(0);
static uint32_t g_retx = 0;
static uint32_t g_rtoCount = 0;
static std::ofstream g_cwndFile;

static void
OnSinkRx(Ptr<const Packet> p, const Address&)
{
    g_rxBytes += p->GetSize();
    g_lastRx = Simulator::Now();
}

static void
OnRetx(Ptr<const Packet>, const TcpHeader&, const Address&, const Address&,
       Ptr<const TcpSocketBase>)
{
    g_retx++;
}

static void
OnCongState(TcpSocketState::TcpCongState_t, TcpSocketState::TcpCongState_t n)
{
    if (n == TcpSocketState::CA_LOSS)
    {
        g_rtoCount++;
    }
}

static void
OnCwnd(uint32_t, uint32_t newVal)
{
    if (g_cwndFile.is_open())
    {
        g_cwndFile << std::fixed << std::setprecision(3) << Simulator::Now().GetSeconds() << ","
                   << newVal / 1448.0 << "\n";
    }
}

static void
ConnectSenderTraces(uint32_t nodeId)
{
    std::ostringstream base;
    base << "/NodeList/" << nodeId << "/$ns3::TcpL4Protocol/SocketList/0/";
    Config::ConnectWithoutContext(base.str() + "Retransmission", MakeCallback(&OnRetx));
    Config::ConnectWithoutContext(base.str() + "CongState", MakeCallback(&OnCongState));
    Config::ConnectWithoutContext(base.str() + "CongestionWindow", MakeCallback(&OnCwnd));
}

// ===========================================================================
int
main(int argc, char* argv[])
{
    std::string mode = "e2e";
    double owdSec = 182.1;   
    std::string rate = "1Mbps";
    uint32_t fileKB = 512;
    int dropPkt = 40;
    std::string cwndFile = "";
    bool defaultConnSetup = false;

    CommandLine cmd(__FILE__);
    cmd.AddValue("mode", "e2e (end-to-end TCP) or split (relay at N1)", mode);
    cmd.AddValue("owdSec", "Total one-way propagation delay Earth->Mars [s]", owdSec);
    cmd.AddValue("rate", "Link data rate of both hops", rate);
    cmd.AddValue("fileKB", "File size to transfer [KB]", fileKB);
    cmd.AddValue("dropPkt", "Data packet index to drop on the last hop (<0 = no loss)", dropPkt);
    cmd.AddValue("cwndFile", "If set, write 'time,cwnd_segments' of the N0 socket to this file",
                 cwndFile);
    cmd.AddValue("defaultConnSetup",
                 "Leave ConnTimeout/InitialEstimation at ns-3 defaults (demonstrates failure)",
                 defaultConnSetup);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(mode != "e2e" && mode != "split", "--mode must be e2e or split");

    // --- TCP configuration -------------------------------------------------
    Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1448));
    Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(16 << 20));
    Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(16 << 20));
    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       TypeIdValue(TcpNewReno::GetTypeId()));

    if (!defaultConnSetup)
    {
        Config::SetDefault("ns3::TcpSocket::ConnTimeout", TimeValue(Seconds(3 * owdSec)));
        Config::SetDefault("ns3::RttEstimator::InitialEstimation", TimeValue(Seconds(2 * owdSec)));
    }

    // --- topology ----------------------------------------------------------
    NodeContainer nodes;
    nodes.Create(3); // 0 = Earth, 1 = Orbiter (relay or router), 2 = Mars

    PointToPointHelper hop;
    hop.SetDeviceAttribute("DataRate", StringValue(rate));
    hop.SetChannelAttribute("Delay", TimeValue(Seconds(owdSec / 2.0)));

    NetDeviceContainer dev01 = hop.Install(nodes.Get(0), nodes.Get(1));
    NetDeviceContainer dev12 = hop.Install(nodes.Get(1), nodes.Get(2));

    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper addr;
    addr.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer if01 = addr.Assign(dev01);
    addr.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer if12 = addr.Assign(dev12);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    if (dropPkt >= 0)
    {
        Ptr<ReceiveListErrorModel> em = CreateObject<ReceiveListErrorModel>();
        em->SetList({static_cast<uint32_t>(dropPkt)});
        dev12.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));
    }

    uint16_t port = 9000;
    uint64_t fileBytes = static_cast<uint64_t>(fileKB) * 1024;

    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer sink = sinkHelper.Install(nodes.Get(2));
    sink.Start(Seconds(0.0));
    sink.Get(0)->TraceConnectWithoutContext("Rx", MakeCallback(&OnSinkRx));

    Address target = (mode == "e2e")
                         ? Address(InetSocketAddress(if12.GetAddress(1), port))
                         : Address(InetSocketAddress(if01.GetAddress(1), port));

    BulkSendHelper srcHelper("ns3::TcpSocketFactory", target);
    srcHelper.SetAttribute("MaxBytes", UintegerValue(fileBytes));
    srcHelper.SetAttribute("SendSize", UintegerValue(1448));
    ApplicationContainer src = srcHelper.Install(nodes.Get(0));
    src.Start(Seconds(1.0));
    g_txStart = Seconds(1.0);

    Ptr<TcpRelay> relay;
    if (mode == "split")
    {
        relay = CreateObject<TcpRelay>();
        relay->Setup(port, InetSocketAddress(if12.GetAddress(1), port));
        nodes.Get(1)->AddApplication(relay);
        relay->SetStartTime(Seconds(0.0));
    }

    if (!cwndFile.empty())
    {
        g_cwndFile.open(cwndFile);
        g_cwndFile << "t,cwnd_segments\n";
    }
    Simulator::Schedule(Seconds(1.001), &ConnectSenderTraces, nodes.Get(0)->GetId());

    // --- run ---------------------------------------------------------------
    Simulator::Stop(Hours(24));
    Simulator::Run();

    double completion = (g_rxBytes == fileBytes) ? (g_lastRx - g_txStart).GetSeconds() : -1.0;
    std::cout << "mode=" << mode << "  owd=" << owdSec << "s  rtt_e2e=" << 2 * owdSec << "s"
              << "  drop=" << dropPkt << "\n"
              << "  bytes_received      = " << g_rxBytes << " / " << fileBytes << "\n"
              << "  completion_time     = " << std::fixed << std::setprecision(1) << completion
              << " s\n"
              << "  sender_retransmits  = " << g_retx << "\n"
              << "  sender_RTO_events   = " << g_rtoCount << "\n";
    if (relay)
    {
        std::cout << "  relay_peak_buffered = " << relay->GetPeakBuffered() << " B\n";
    }

    if (g_cwndFile.is_open())
    {
        g_cwndFile.close();
    }
    Simulator::Destroy();
    return 0;
}
