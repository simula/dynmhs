# Dynamic Multi-Homing Setup (DynMHS)

## Description

DynMHS dynamically sets up IP routing rules, to allow using multiple network connections simultaneously. That is, for each relevant network interface, a separate routing table is created and maintained. For each source address of a network interface managed by DynMHS, routing rules are maintained to point to the corresponding routing table. Software binding to a specific interface address can then use a specific network. In addition, multi-homing-capable network protocols like the Multi-Path TCP (MPTCP) or the Stream Control Transmission Protocol (SCTP) can take advantage of multi-homing for redundancy and load balancing.

See the manpage of dynmhs for details!

## Usage Examples

```
dynmhs --version
sudo dynmhs --interface eno1:1000 --interface eno2:1002 --loglevel 0
sudo dynmhs -I ethernet:1000 -I telia:2000 -I telenor:3000 --loglevel 2
sudo dynmhs -I ethernet:1000 -I telia:2000 -I telenor:3000 --logcolor off
```

## Binary Package Installation

Please use the issue tracker at https://github.com/simula/dynmhs/issues to report bugs and issues!

### Ubuntu Linux

For ready-to-install Ubuntu Linux packages of DynMHS, see Launchpad PPA for Thomas Dreibholz!

```
sudo apt-add-repository -sy ppa:dreibh/ppa
sudo apt-get update
sudo apt-get install dynmhs
```

### Fedora Linux

For ready-to-install Fedora Linux packages of DynMHS, see COPR PPA for Thomas Dreibholz!

```
sudo dnf copr enable -y dreibh/ppa
sudo dnf install dynmhs
```

## Sources Download

DynMHS is released under the GNU General Public Licence (GPL).

Please use the issue tracker at https://github.com/simula/dynmhs/issues to report bugs and issues!

### Development Version

The Git repository of the DynMHS sources can be found at https://github.com/simula/dynmhs:

- Issue tracker: https://github.com/simula/dynmhs/issues.
  Please submit bug reports, issues, questions, etc. in the issue tracker!

- Pull Requests for DynMHS: https://github.com/simula/dynmhs/pulls.
  Your contributions to DynMHS are always welcome!

- CI build tests of DynMHS: https://github.com/simula/dynmhs/actions.

### Current Stable Release

See https://www.nntb.no/~simula/dynmhs/#Download!
