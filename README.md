<h1 align="center">
 Dynamic Multi-Homing Setup&nbsp;(DynMHS)<br />
 <span style="font-size: 75%;">A Tool for the Dynamic Setup of Multi-Homing IP Routing Rules</span><br />
 <a href="https://www.nntb.no/~dreibh/dynmhs/">
  <img alt="DynMHS Logo" src="src/figures/Logo-DynMHS.svg" width="25%" /><br />
  <span style="font-size: 75%;">https://www.nntb.no/~dreibh/dynmhs</span>
 </a>
</h1>


# 💡 What is DynMHS?

Dynamic Multi-Homing Setup&nbsp;(DynMHS) dynamically sets up IP routing rules, to allow for using multiple network connections simultaneously. That is, for each relevant network interface, a separate routing table is created and maintained. For each source address of a network interface managed by DynMHS, routing rules are maintained to point to the corresponding routing table. Software binding to a specific interface address can then use a specific network. In addition, multi-homing-capable network protocols like the Multi-Path TCP&nbsp;(MPTCP) or the Stream Control Transmission Protocol&nbsp;(SCTP) can take advantage of multi-homing for redundancy and load balancing.


# 😀 Example

## Scenario

A Linux PC is connected to two NAT networks, configuration is dynamic via IPv4 DHCP and IPv6 auto-configuration:

* Network #1 on interface enp0s8: 172.30.255.4 / fdff:b44d:605c:0:a00:27ff:fedb:ad69
* Network #2 on interface enp0s9: 192.168.255.4 / fdc9:dc25:8e35:0:a00:27ff:feaa:bc91

Network settings for [Netplan](https://netplan.io/) (in `/etc/netplan/testpc.yaml`):
```yaml
network:
  version: 2
  ethernets:
    enp0s8:
      accept-ra: true
      dhcp4: true
      dhcp4-overrides:
        route-metric: 200
    enp0s9:
      accept-ra: true
      dhcp4: true
      dhcp4-overrides:
        route-metric: 300
```

```bash
sudo hipercontracer -P \
   -S 172.30.255.4 -S 192.168.255.4 \
   -S fdff:b44d:605c:0:a00:27ff:fedb:ad69 -S fdc9:dc25:8e35:0:a00:27ff:feaa:bc91 \
   -D 8.8.8.8 -D 2001:4860:4860::8888
```

IPv4 routes:

<pre><code><span style="color:red;">user@host</span><span style="color:blue;">:~</span><span style="color:gray;">$</span> <span class="ex">ip</span> <span class="at">-4</span> route show
default via 172.30.255.1 dev enp0s8 proto dhcp src 172.30.255.4 metric 200
default via 192.168.255.1 dev enp0s9 proto dhcp src 192.168.255.4 metric 300
172.30.255.0/24 dev enp0s8 proto kernel scope link src 172.30.255.4 metric 200
172.30.255.1 dev enp0s8 proto dhcp scope link src 172.30.255.4 metric 200
192.168.255.0/24 dev enp0s9 proto kernel scope link src 192.168.255.4 metric 300
192.168.255.1 dev enp0s9 proto dhcp scope link src 192.168.255.4 metric 300
</code></pre>

IPv6 routes:

<pre><code><span style="color:red;">user@host</span><span style="color:blue;">:~</span><span style="color:gray;">$</span> <span class="ex">ip</span> <span class="at">-6</span> route show
fc88:1::/64 dev hostonly101 proto kernel metric 256 linkdown pref medium
fdc9:dc25:8e35::/64 dev enp0s9 proto ra metric 300 pref medium
fdff:b44d:605c::/64 dev enp0s8 proto ra metric 200 pref medium
fe80::/64 dev enp0s8 proto kernel metric 256 pref medium
fe80::/64 dev enp0s9 proto kernel metric 256 pref medium
default via fe80::5054:ff:fe12:3500 dev enp0s8 proto ra metric 200 expires 788sec pref medium
default via fe80::5054:ff:fe12:3500 dev enp0s9 proto ra metric 300 expires 772sec pref medium
</code></pre>

Note the two default routes with their different metrics (200, 300).

## Why is this setup not working as expected?

The expectation is that, according to the chosen source address, a packet is routed via the corresponding interface (enp0s8 or enp0s9). This can be tested by using [HiPerConTracer](https://www.nntb.no/~dreibh/hipercontracer/), running HiPerConTracer Ping to the Google DNS servers (8.8.8.8, 2001:4860:4860::8888) from all four source addresses:

<pre><code><span style="color:red;">user@host</span><span style="color:blue;">:~</span><span style="color:gray;">$</span> <span class="fu">sudo</span> hipercontracer <span class="at">-P</span> \
   <span class="at">-S</span> 172.30.255.4 <span class="at">-S</span> 192.168.255.4 \
   <span class="at">-S</span> fdff:b44d:605c:0:a00:27ff:fedb:ad69 <span class="at">-S</span> fdc9:dc25:8e35:0:a00:27ff:feaa:bc91 \
   <span class="at">-D</span> 8.8.8.8 <span class="at">-D</span> 2001:4860:4860::8888
</code></pre>

Connectivity is always over the primary interface, i.e.&nbsp;172.30.255.4 and&nbsp;fdff:b44d:605c:0:a00:27ff:fedb:ad69. The reason is: This default route has the lowest metric! Also, simply using the same metric for both routes does *not* fix the issue. Then, just the first default route in the routing table would get used.

To get the setup working as expected, it is necessary to configure separate routing tables for each network, and routing rules to select a routing table according to the *source* IP address. For example:

* Rule #2000: for packets from 172.30.255.4 use routing table #2000.
* Rule #2000: for packets from fdff:b44d:605c:0:a00:27ff:fedb:ad69 use routing table #2000.
* Rule #3000: for packets from 192.168.255.4 use routing table #3000.
* Rule #3000: for packets from fdc9:dc25:8e35:0:a00:27ff:feaa:bc91 use routing table #3000.

Rules:

<pre><code><span style="color:red;">user@host</span><span style="color:blue;">:~</span><span style="color:gray;">$</span> <span class="ex">ip</span> rule show
0:      from all lookup local
2000:   from 172.30.255.4 lookup 2000
3000:   from 192.168.255.4 lookup 3000
32766:  from all lookup main
32767:  from all lookup default

<span style="color:red;">user@host</span><span style="color:blue;">:~</span><span style="color:gray;">$</span> <span class="ex">ip</span> <span class="at">-6</span> rule show
0:      from all lookup local
2000:   from fdff:b44d:605c:0:a00:27ff:fedb:ad69 lookup 2000
3000:   from fdc9:dc25:8e35:0:a00:27ff:feaa:bc91 lookup 3000
32766:  from all lookup main
</code></pre>

Tables:

<pre><code><span style="color:red;">user@host</span><span style="color:blue;">:~</span><span style="color:gray;">$</span> <span class="ex">ip</span> route show table 2000
default via 172.30.255.1 dev enp0s8 proto dhcp src 172.30.255.4 metric 200
172.30.255.0/24 dev enp0s8 proto kernel scope link src 172.30.255.4 metric 200
172.30.255.1 dev enp0s8 proto dhcp scope link src 172.30.255.4 metric 200

<span style="color:red;">user@host</span><span style="color:blue;">:~</span><span style="color:gray;">$</span> <span class="ex">ip</span> <span class="at">-6</span> route show table 2000
fdff:b44d:605c::/64 dev enp0s8 proto ra metric 200 pref medium
fe80::/64 dev enp0s8 proto kernel metric 256 pref medium
default via fe80::5054:ff:fe12:3500 dev enp0s8 proto ra metric 200 pref medium

<span style="color:red;">user@host</span><span style="color:blue;">:~</span><span style="color:gray;">$</span> <span class="ex">ip</span> route show table 3000
default via 192.168.255.1 dev enp0s9 proto dhcp src 192.168.255.4 metric 300
192.168.255.0/24 dev enp0s9 proto kernel scope link src 192.168.255.4 metric 300
192.168.255.1 dev enp0s9 proto dhcp scope link src 192.168.255.4 metric 300

<span style="color:red;">user@host</span><span style="color:blue;">:~</span><span style="color:gray;">$</span> <span class="ex">ip</span> <span class="at">-6</span> route show table 3000
fdc9:dc25:8e35::/64 dev enp0s9 proto ra metric 300 pref medium
fe80::/64 dev enp0s9 proto kernel metric 256 pref medium
default via fe80::5054:ff:fe12:3500 dev enp0s9 proto ra metric 300 pref medium
</code></pre>

It would be possible to configure *static* rules/tables in Netplan. But DHCP and IPv6 auto-configuration use *dynamic* addresses. So, they may change!

## Applying DynMHS

Dynamic Multi-Homing Setup&nbsp;(DynMHS) is the solution for dynamically creating, adapting, and destroying routing tables and rules. DynMHS monitors the system's network configuration for changes, and applies the necessary settings for additional routing tables and the corresponding routing rules. This works for IPv4 and IPv6, including multiple addresses as well as additional routes over the monitored interfaces.

### Manual usage

```bash
sudo dynmhs --interface enp0s8:2000 --interface enp0s9:3000 --loglevel 2
```

### Running as SystemD service

Configuration in `/etc/dynmhs/dynmhs.conf`:

```bash
# ====== Logging Verbosity ==================================================
# 0=trace, 1=debug, 2=info, 3=warning, 4=error, 5=fatal
LOGLEVEL=2

# ====== Options ============================================================
NETWORK1="enp0s8:2000"
NETWORK2="enp0s9:3000"
NETWORK3=""
NETWORK4=""
NETWORK5=""
```

These settings map interface enp0s8 to routing table #2000, and interface enp0s9 to routing table #3000. DynMHS will maintain the tables, and the corresponding rules.

To enable and start the DynMHS service:

```bash
sudo systemctl daemon-reload
sudo systemctl enable dynmhs
sudo systemctl start dynmhs
```

To observe the logs of the DynMHS service:

```bash
sudo journalctl -f -u dynmhs
```

### A HiPerConTracer Ping test

Another test with [HiPerConTracer](https://www.nntb.no/~dreibh/hipercontracer/), running HiPerConTracer Ping to the Google DNS servers from all four source addresses:

<pre>
<span style="color:red;">user@host</span><span style="color:blue;">:~</span><span style="color:gray;">$</span> sudo hipercontracer -P \
   -S 172.30.255.4 -S 192.168.255.4 \
   -S fdff:b44d:605c:0:a00:27ff:fedb:ad69 -S fdc9:dc25:8e35:0:a00:27ff:feaa:bc91 \
   -D 8.8.8.8 -D 2001:4860:4860::8888
...
2025-02-28 13:59:50.422: Ping ICMP  192.168.255.4                           8.8.8.8                                 Success  s: 27µs q:  4µs r: 54µs  A:8.892ms   S:8.808ms   H:---
2025-02-28 13:59:50.422: Ping ICMP  fdff:b44d:605c:0:a00:27ff:fedb:ad69     2001:4860:4860::8888                    Success  s: 51µs q:  6µs r:246µs  A:8.634ms   S:8.331ms   H:---
2025-02-28 13:59:50.422: Ping ICMP  172.30.255.4                            8.8.8.8                                 Success  s: 38µs q:  4µs r:125µs  A:9.272ms   S:9.105ms   H:---
2025-02-28 13:59:50.422: Ping ICMP  fdc9:dc25:8e35:0:a00:27ff:feaa:bc91     2001:4860:4860::8888                    Success  s: 29µs q:  2µs r:162µs  A:8.651ms   S:8.458ms   H:---
...
</pre>

Now, there is connectivity over both interfaces!


# 📦 Binary Package Installation

Please use the issue tracker at [https://github.com/simula/dynmhs/issues](https://github.com/simula/dynmhs/issues) to report bugs and issues!

## Ubuntu Linux

For ready-to-install Ubuntu Linux packages of DynMHS, see [Launchpad PPA for Thomas Dreibholz](https://launchpad.net/~dreibh/+archive/ubuntu/ppa/+packages?field.name_filter=dynmhs&field.status_filter=published&field.series_filter=)!

```bash
sudo apt-add-repository -sy ppa:dreibh/ppa
sudo apt-get update
sudo apt-get install dynmhs
```

## Fedora Linux

For ready-to-install Fedora Linux packages of DynMHS, see [COPR PPA for Thomas Dreibholz](https://copr.fedorainfracloud.org/coprs/dreibh/ppa/package/dynmhs/)!

```bash
sudo dnf copr enable -y dreibh/ppa
sudo dnf install dynmhs
```


# 💾 Build from Sources

DynMHS is released under the [GNU General Public Licence&nbsp;(GPL)](https://www.gnu.org/licenses/gpl-3.0.en.html#license-text).

Please use the issue tracker at [https://github.com/simula/dynmhs/issues](https://github.com/simula/dynmhs/issues) to report bugs and issues!

## Development Version

The Git repository of the DynMHS sources can be found at [https://github.com/simula/dynmhs](https://github.com/simula/dynmhs):

```bash
git clone https://github.com/simula/dynmhs
cd dynmhs
sudo ci/get-dependencies --install
cmake .
make
```

Note: The script [`ci/get-dependencies`](https://github.com/simula/dynmhs/blob/master/ci/get-dependencies) automatically  installs the build dependencies under Debian/Ubuntu Linux, Fedora Linux, and FreeBSD. For manual handling of the build dependencies, see the packaging configuration in [debian/control](https://github.com/simula/dynmhs/blob/master/debian/control) (Debian/Ubuntu Linux), [dynmhs.spec](https://github.com/simula/dynmhs/blob/master/rpm/dynmhs.spec) (Fedora Linux), and [Makefile](https://github.com/simula/dynmhs/blob/master/freebsd/dynmhs/Makefile) FreeBSD.

Contributions:

* Issue tracker: [https://github.com/simula/dynmhs/issues](https://github.com/simula/dynmhs/issues).
  Please submit bug reports, issues, questions, etc. in the issue tracker!

* Pull Requests for DynMHS: [https://github.com/simula/dynmhs/pulls](https://github.com/simula/dynmhs/pulls).
  Your contributions to DynMHS are always welcome!

* CI build tests of DynMHS: [https://github.com/simula/dynmhs/actions](https://github.com/simula/dynmhs/actions).

## Release Versions

See [https://www.nntb.no/~simula/dynmhs/#current-stable-release](https://www.nntb.no/~simula/dynmhs/#current-stable-release) for the release packages!


# 🔗 Useful Links

* [HiPerConTracer – High-Performance Connectivity Tracer](https://www.nntb.no/~dreibh/hipercontracer/)
* [NetPerfMeter – A TCP/MPTCP/UDP/SCTP/DCCP Network Performance Meter Tool](https://www.nntb.no/~dreibh/netperfmeter/)
* [SubNetCalc – An IPv4/IPv6 Subnet Calculator](https://www.nntb.no/~dreibh/dynmhs/)
* [System-Tools – Tools for Basic System Management](https://www.nntb.no/~dreibh/system-tools/)
* [Wireshark](https://www.wireshark.org/)
