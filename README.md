# MemLiner README

We prepared **four machines** (two CPU server - memory server pairs) with InfiniBand plugged in for artifact evaluation. The configuration of these four machines are not exactly the same as the servers we used in the paper, but you should be able to see similar speedups. 

<aside>
💡 Warning: please confirm that no other users are using the servers at the same time.  You can use command `w`  and `tmux ls` to check other logins.


</aside>

# 1. Configuration & Execution

MemLiner has been configured on our provided machines. You can execute the applications directly on these machines. If you want to build all the things from the scratch, please check Section 3.

## 1.1 Connect the CPU Server with the memory server

Please firstly check if the CPU server and memory server is already connected. You can use the following command:

```bash
dmesg | grep -A5 "rswap_query_available_memory: Got 12 free memory chunks from remote memory server. Request for Chunks"
```

If you can see the following output:

```bash
[  208.689215] rswap_query_available_memory: Got 12 free memory chunks from remote memory server. Request for Chunks
[  208.689222] two_sided_message_done, Got a WC from CQ, IB_WC_SEND. 2-sided RDMA post done.
[  208.689253] two_sided_message_done, Got a WC from CQ, IB_WC_RECV.
[  208.689255] rswap_request_for_chunk, Got 12 chunks from memory server.
[  208.689256] rdma_session_connect,Exit the main() function with built RDMA conenction rdma_session_context:0xffffffffc0a8c420 .
[  208.689259] frontswap module loaded
```

Then it means the servers are already connected, you can directly goto section 1.2. If not, then check our instructions below:

- Launch the memory server:

```bash
# Log into memory server, .e.g., guest@zion-4.cs.ucla.edu

# Let memory server run in background
tmux

# Launch the memory server and wait the connection from CPU serever
cd ${HOME}/MemLiner/scripts/server
./rswap-server
```

- Launch the CPU server:

```bash
# Log into the CPU server, .e.g., guest@zion-1.cs.ucla.edu
# Warning: Please check other users didn't connect the CPU server with any memory servers. 
# Please check the FAQ Q#1 to see how to confirm the situation.  
#
cd ${HOME}/MemLiner/scripts/client
./manage_rswap_client.sh install
```

## 1.2 Limit the memory resources of applications

MemLiner relies on *cgroup* to limit the memory usage of applications running on the CPU server. we are using cgroup version 1 here. 

Create cgroup on the CPU server:

```bash
# Create a memory cgroup, memctl, for the applications running on the CPU server.
# $USER is the username of the account. It's "guest" here.
sudo cgcreate -t $USER -a $USER -g memory:/memctl

# Please confirm that a memory cgroup directory is built as:
/sys/fs/cgroup/memory/memctl
```

Limit local memory size on the CPU server. E.g., set local memory to 9GB:

```bash
# Please adjust the local memory size for different applications.
# e.g., for Spark with a 32GB Java heap. 25% local memory ratio means that we limit its CPU server memory size to 9GB.
# Application's working set includeds both Java heap and off-heap data, which is around 36GB in total. 
echo 9g > /sys/fs/cgroup/memory/memctl/memory.limit_in_bytes
```

## 1.3 Run Spark applications on MemLiner

Here we use Apache Spark as an example to show how to execute the applications on the MemLiner. If you want to execute other applications, please check the **Section 2**.  All the following operations are on the CPU server, .e.g. guest@zion-1.cs.ucla.edu.

## 1.3.1 All-in-one shell script

We put all the necessary instructions into an all-in-on shell script, the users can execute  the applications, collect the experiment results and draw the pictures via the the single shell-script: `${HOME}/MemLinerScripts/run-all.sh`

For example, to execute the Spark applications on the pair: `guest@zion-1`（CPU server）and `guest@zion-4` (Memory server)

```bash
# switch to the directory
cd ${HOME}/MemLinerScripts

# launch tmux
tmux

# Run spark applicaions
# ./run-all.sh app_name
# app_name list: tradesoap, spark, neo4j, cas, qcd
./run-all.sh spark

# The results are in ${HOME}/MemLinerTimeLogs/evaluation_51.pdf
```

# 2.  Run other applications

We list how to run the other applications in this session: (1) Graph database, Neo4j; (2) Online stock trading emulating system, DayTrader; (3) NoSQL database Cassandra; (4) In-memory key-value store Quickcached.

To run all applications together, you can use our all-in-one shell script:

```bash
# switch to the directory
cd ${HOME}/MemLinerScripts

# launch tmux
tmux

# Run all applicaions
./run-all.sh

# The results will be generated under ${HOME}/MemLinerTimeLogs/evaluation51-ae.pdf
```

If you want to run different applications individually, please check instructions below. All results will be in the folder: `${HOME}/MemLinerTimeLogs`. The name of the generated figure is `evaluation51-ae.pdf`.

## 2.1 Neo4J

All the following operations are done on CPU server, e.g., `guest@zion-1.cs.ucla.edu`.

```bash
# switch to the directory
cd ${HOME}/MemLinerScripts

# launch tmux
tmux

# Run neo4j applicaions
./run-all.sh neo4j
```

## 2.2  TradeSoap

Assume we are on the CPU server, e.g., `guest@zion-1.cs.ucla.edu`.  ****

```bash
# switch to the directory
cd ${HOME}/MemLinerScripts

# launch tmux
tmux

# Run tradesoap
./run-all.sh tradesoap
```

### Known problems:

You might encounter AxisFault error when running baseline or MemLiner. You can just ignore it because that’s a confirmed bug in tradesoap impelementation.

## 2.3  Cassandra

Assume we are on the CPU server, e.g., guest@zion-1.cs.ucla.edu. The application is in the folder: `${HOME}/cassandra`

```bash
# switch to the directory
cd ${HOME}/MemLinerScripts

# launch tmux
tmux

# Run cassandra applicaions
./run-all.sh cas
```

## 2.4 Running QuickCached

Assume we are on the CPU server, e.g., guest@zion-1.cs.ucla.edu. The application is put in folder: `${HOME}/quickcached`

```bash
# switch to the directory
cd ${HOME}/MemLinerScripts

# launch tmux
tmux

# Run quickcached applicaions
./run-all.sh qcd
```

# 3. Build & Install MemLiner

## 3.1 Environments

Two sets of environment have been tested for MemLiner:

```bash
Ubuntu 18.04
Linux 5.4
OpenJDK 12.04
GCC 5.5 
MLNX_OFED rriver 4.9-2.2.4.0

or

CentOS 7.5 - 7.7
Linux 5.4
OpenJDK 12.04
GCC 5.5
MLNX_OFED driver 4.9-2.2.4.0 
```

Among the requirements, the Linux version, OpenJDK version and MLNX-OFED driver version are guaranteed during the build & installation process below. Just make sure that the Linux distribution version and gcc version are correct.

## 3.2 Install Kernel

Next we will use Ubuntu 18.04 as an example to show how to build and install the kernel:

### Change the kernel on both CPU and memory server:

Change the grub parameters

```bash
sudo vim /etc/default/grub

# Choose the bootup kernel version as 5.4.0
GRUB_DEFAULT="Advanced options for Ubuntu>Ubuntu, with Linux 5.4.0"

# Change the value of GRUB_CMDLINE_LINUX:
GRUB_CMDLINE_LINUX="nokaslr transparent_hugepage=madvise intel_pstate=disable idle=poll processor.max_cstate=0 intel_idle.max_cstate=0"

```

Build the Kernel source code && install it:

```bash
# Change to the kernel folder:
cd MemLiner/Kernel

sudo ./build_kernel.sh build
sudo ./build_kernel.sh install
sudo reboot
```

## 3.3 Install MLNX OFED Driver

**Preparations:**

MemLiner is only tested on `MLNX_OFED-4.9-2.2.4.0`. Download and unzip the package according to your system version, on both CPU and memory server.

Take Ubuntu 18.04 as an example:

### 3.3.1 Download & Install the MLNX_OFED driver

```bash
# Download the MLNX OFED driver for the Ubuntu 18.04
wget https://content.mellanox.com/ofed/MLNX_OFED-4.9-2.2.4.0/MLNX_OFED_LINUX-4.9-2.2.4.0-ubuntu18.04-x86_64.tgz
tar xzf MLNX_OFED_LINUX-4.9-2.2.4.0-ubuntu18.04-x86_64.tgz
cd MLNX_OFED_LINUX-4.9-2.2.4.0-ubuntu18.04-x86_64

# Remove the incompatible libraries
sudo apt remove ibverbs-providers:amd64 librdmacm1:amd64 librdmacm-dev:amd64 libibverbs-dev:amd64 libopensm5a libosmvendor4 libosmcomp3 -y

# Install the MLNX OFED driver against the kernel 5.4.0
sudo ./mlnxofedinstall --add-kernel-support
```

### 3.3.2 Enable the *opensm* and *openibd* services

(1) Enable and start the ***openibd*** service

```bash
sudo systemctl enable openibd
sudo systemctl start  openibd

# confirm the service is running and enabled:
sudo systemctl status openibd

# the log shown as:
● openibd.service - openibd - configure Mellanox devices
   Loaded: loaded (/lib/systemd/system/openibd.service; enabled; vendor preset: enabled)
   Active: active (exited) since Mon 2022-05-02 14:40:53 CST; 1min 24s ago
    
```

(2) Enable and start the ***opensmd*** service:

```bash
sudo systemctl enable opensmd
sudo systemctl start opensmd

# confirm the service status
sudo systemctl status opensmd

# the log shown as:
opensmd.service - LSB: Manage OpenSM
   Loaded: loaded (/etc/init.d/opensmd; generated)
   Active: active (running) since Mon 2022-05-02 14:53:39 CST; 10s ago

#
# Warning: you may encounter the problem:
#
opensmd.service is not a native service, redirecting to systemd-sysv-install.
Executing: /lib/systemd/systemd-sysv-install enable opensmd
update-rc.d: error: no runlevel symlinks to modify, aborting!

#
# Please refer to the **Question #2** in FAQ for how to solve this problem
#
```

### 3.3.3 Confirm the InfiniBand is available

Check the InfiniBand information

```bash
# Get the InfiniBand information
ibstat

# the log shown as:
# Adapter's stat should be Active.

	Port 1:
		State: Active
		Physical state: LinkUp
		Rate: 100
		Base lid: 3
		LMC: 0
		SM lid: 3
		Capability mask: 0x2651e84a
		Port GUID: 0x0c42a10300605e88
		Link layer: InfiniBand
```

### 3.3.4 For other OS distributions

(1)  For CentOS 7.7

Install the necessary libraries

```bash
sudo yum install -y gtk2 atk cairo tcl tk
```

Download the MLNX OFED driver for CentOS 7.7

```bash
wget https://content.mellanox.com/ofed/MLNX_OFED-4.9-2.2.4.0/MLNX_OFED_LINUX-4.9-2.2.4.0-rhel7.7-x86_64.tgz
```

And then, repeat the steps from 3.3.1 to 3.3.3.

## 3.4 Build the RemoteSwap data path

The user needs to build the rswap on both the CPU server and memory servers.

### 3.4.1  Configuration

(1) IP configuration

Assign memory server’s ip address to both the CPU server and memory servers. Take guest@zion-4.cs.ucla.edu  as example. It’s InfiniBand IP address is (IP:10.0.0.4) : 

```cpp
// (1) CPU server
// Repalce the client/rswap_rdma.c:783:	char ip[] = "memory.server.ib.ip";
// to:
${HOME}/Memliner/scrips/client/rswap_rdma.c:783:	char ip[] = "10.0.0.4";

// (2) Memory server
// Replace the server/rswap_server.cpp:61:	const char *ip_str = "memory.server.ib.ip";
// to:
${HOME}/server/rswap_server.cpp:61:	const char *ip_str = "10.0.0.4";
```

(2) available cores configuration

Replace the macro, `ONLINE_CORES`, defined in `MemLiner/scripts/server/rswap_server.hpp` to the number of cores of the CPU server (which can be printed by command line, `nproc` , on the CPU server.)

```cpp
${HOME}/MemLiner/scripts/server/rswap_server.hpp:38: #define ONLINE_CORES 16
```

### 3.4.2 Build the RemoteSwap datapah

Build the client end on CPU server, e.g., guest@zion-1.cs.ucla.edu

```bash
cd ${HOME}/MemLiner/scripts/client
make clean && make
```

(2) Build the server end on memory server, e.g., guset@zion-4.cs.ucla.edu

```bash
cd ${HOME}/MemLiner/scripts/server
make clean && make
```

And then, please refer to **Section 1.1** for how to connect the CPU server and the memory server.

## 3.5 Build MemLiner (OpenJDK)

Build MemLiner JDK. Please download and install jdk-12.0.2 and other dependent libraries to build the MemLiner (OpenJDK)

```bash
cd ${HOME}/MemLiner/JDK
./configure --with-boot-jdk=$HOME/jdk-12.0.2 --with-debug-level=release
make JOBS=32
```

# FAQ

## Question#1  Enable opensmd service in Ubuntu 18.04

### Error message:

```bash
opensmd.service is not a native service, redirecting to systemd-sysv-install.
Executing: /lib/systemd/systemd-sysv-install enable opensmd
update-rc.d: error: no runlevel symlinks to modify, aborting!
```

### 1.1 Update the service start level in /etc/init.d/opensmd

The original /etc/init.d/opensmd 

```bash
  8 ### BEGIN INIT INFO
  9 # Provides: opensm
 10 # Required-Start: $syslog openibd
 11 # Required-Stop: $syslog openibd
 12 # Default-Start: null
 13 # Default-Stop: 0 1 6
 14 # Description:  Manage OpenSM
 15 ### END INIT INFO
```

Change  /etc/init.d/opensmd  to :

```bash
12 # Default-Start: 2 3 4 5
```

### 1.2 Enable && Start the *opensmd* service

```bash
sudo update-rc.d opensmd remove -f
sudo systemctl enable opensmd
sudo systemctl start opensmd

# confirm the service status
sudo systemctl status opensmd

# The log shown as:
opensmd.service - LSB: Manage OpenSM
   Loaded: loaded (/etc/init.d/opensmd; generated)
   Active: active (running) since Mon 2022-05-02 14:53:39 CST; 10s ago
```
