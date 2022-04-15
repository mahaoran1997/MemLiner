# MemLiner

## 1. Build & Install

MemLiner has been configured on our machines. You can skip this section for artifact evaluation. You can follow the following steps in this section if you want to build and install MemLiner on your own machine.

### 1.1 Install Kernel

```python
cd MemLiner/Kernel
sudo ./build_kernel.sh build
sudo ./build_kernel.sh install
```

### 1.2 Install MLNX_OFED Driver

```python
# for CentOS 7.5
wget https://content.mellanox.com/ofed/MLNX_OFED-4.9-2.2.4.0/MLNX_OFED_LINUX-4.9-2.2.4.0-rhel7.5-x86_64.tgz
tar xzf MLNX_OFED_LINUX-4.9-2.2.4.0-rhel7.5-x86_64.tgz
cd MLNX_OFED_LINUX-4.9-2.2.4.0-rhel7.5-x86_64
# for CentOS 7.6
wget https://content.mellanox.com/ofed/MLNX_OFED-4.9-2.2.4.0/MLNX_OFED_LINUX-4.9-2.2.4.0-rhel7.6-x86_64.tgz
tar xzf MLNX_OFED_LINUX-4.9-2.2.4.0-rhel7.6-x86_64.tgz
cd MLNX_OFED_LINUX-4.9-2.2.4.0-rhel7.6-x86_64
# for CentOS 7.7
wget https://content.mellanox.com/ofed/MLNX_OFED-4.9-2.2.4.0/MLNX_OFED_LINUX-4.9-2.2.4.0-rhel7.7-x86_64.tgz
tar xzf MLNX_OFED_LINUX-4.9-2.2.4.0-rhel7.7-x86_64.tgz
cd MLNX_OFED_LINUX-4.9-2.2.4.0-rhel7.7-x86_64
# for Ubuntu 18.04
wget https://content.mellanox.com/ofed/MLNX_OFED-4.9-2.2.4.0/MLNX_OFED_LINUX-4.9-2.2.4.0-ubuntu18.04-x86_64.tgz
tar xzf MLNX_OFED_LINUX-4.9-2.2.4.0-ubuntu18.04-x86_64.tgz
cd MLNX_OFED_LINUX-4.9-2.2.4.0-ubuntu18.04-x86_64\

# for CentOS
sudo yum install -y gtk2 atk cairo tcl tk
# for Ubuntu (only applies when a InfiniBand driver was installed via apt, but safe to run anyway)
# opensm may be running to prevent a complete uninstallation process, stop it first
sudo service opensm stop
sudo apt remove ibverbs-providers:amd64 librdmacm1:amd64 librdmacm-dev:amd64 libibverbs-dev:amd64 libopensm5a libosmvendor4 libosmcomp3
# install driver. additional packages may need to be installed, just follow the output
sudo ./mlnxofedinstall --add-kernel-support
# update initramfs (see prompt of installation, may not be needed)
sudo dracut -f
# enable driver
sudo /etc/init.d/openibd restart

# to uninstall the driver
sudo ./uninstall.sh

# On Ubuntu, a bug needs to be fixed every time after the driver is installed:
# delete old symlinks
sudo update-rc.d opensmd remove -f
# find the line `# Default start` and change it to `# Default-Start: 2 3 4 5`
sudo vim /etc/init.d/opensmd
sudo systemctl enable opensmd
# before reboot, may need to manually enable the service
sudo service opensmd start
# check the status of the service, should be active
service opensmd status
```

### 1.3 Build remoteswap

```python
cd MemLiner/scripts
## change IP in code into IP of the memory server
## search "10.0.0.4" in remoteswap folder, and change the 2 IP occurances with that of ib0 on memory server, on both servers

# On memory server:
cd scripts/server
make clean && make

# On CPU server:
## change $home_dir in client/manage_rswap_client.sh to your home directory
cd scripts/client
make clean && make
```

### 1.4 Running remoteswap

```python
# launch remoteswap server on memory server
cd MemLiner/scripts/server
tmux
./rswap-server

# launch remoteswap client on CPU server
cd MemLiner/scripts/client
./manage_rswap_client.sh install

# create cgroup to limit local memory size
# on CPU server
sudo cgcreate -t $USER -a $USER -g memory:/memctl
```

### 1.5 Build JVM

```python
# Download JDK12 to your home folder. Suppose its path is PATH
cd MemLiner/JDK
./configure --with-boot-jdk=PATH --with-debug-level=release
make JOBS=32
```

When following the instructions below to run MemLiner on your machines, Zion-1 refers to the CPU server and Zion-4 refers to the memory server.

## 2. Running Spark

All the following operations are on Zion-1.

First change directory to the spark folder on Zion-1 and start the master and worker:

```bash
cd /mnt/ssd/guest/spark-3.0.0-preview2-bin-hadoop3.2
```

Configure the default JDK to unmodified JDK and start the master and worker:

```bash
sudo update-alternatives --config java
# select 1

./sbin/start-all.sh
```

After editing `conf/spark-defaults.conf` according to instructions below, restart the master and worker to make sure the new config is used.

```bash
./sbin/stop-all.sh
./sbin/start-all.sh
```

After done with app spark apps, stop the master and worker

```bash
./sbin/stop-all.sh
```

### 2.1 Baseline

1. Edit the `conf/spark-defaults.conf` , comment Line 43 and uncomment Line 42, and restart master and worker.
2. Use the following command to run baseline:
    
    ```bash
    # application: lr, km, tc
    # local_ratio: 100, 25, 13
    ./scripts/baseline.sh application local_ratio 
    ```
    

### 2.2 MemLiner

1. Edit the `conf/spark-defaults.conf` , comment Line 42 and uncomment Line 43, and restart master and worker.
2. Use the following command to run MemLiner:
    
    ```bash
    # application: lr, km, tc
    # local_ratio: 25, 13
    ./scripts/memliner.sh application local_ratio 
    ```
    

## 3. Running Neo4J

All the following operations are on Zion-1.

Change directory to the Neo4j folder on Zion-1:

```bash
cd /mnt/ssd/guest/neo4j
```

1. Configure the JDK:
    
    ```bash
    sudo update-alternatives --config java
    # select 1 for baseline and select 2 for MemLiner.
    ```
    
2. Open `/etc/neo4j/neo4j.conf` and uncomment the lines that contains the corresponding parameters.
3. Start Neo4J. Wait until the log `INFO Started.` is printed before running the apps.
    
    ```bash
    # tool: baseline, memliner
    # local_ratio: 100, 25, 13
    ./start-neo4j.sh tool local_ratio
    ```
    
4. Running applications. Use `bash` to make `time -p` functional.
    1. Neo4J PageRank
        
        ```python
        (time -p (cypher-shell -u neo4j -p neo4j -f Neo4j/apoc_batch_pagerank_stats.cypher) 2>&1) | tee -a logs/npr.log
        ```
        
    2. Neo4J Triangle Counting
        
        ```python
        (time -p (cypher-shell -u neo4j -p neo4j -f Neo4j/apoc_batch_triangle_count_stats.cypher) 2>&1) | tee -a logs/ntc.log
        ```
        
    3. Neo4J Degree Centrality
        
        ```python
        (time -p (cypher-shell -u neo4j -p neo4j -f Neo4j/apoc_batch_degree_centrality_stats_mutate.cypher) 2>&1) | tee -a logs/ndc.log
        ```
        
5. Use `ctrl+C` Stop the Neo4j server after running each application and repeat from step 1.

## 4. Running Cassandra

Change directory to the Cassandra folder on Zion-1:

```python
cd /mnt/ssd/guest/cassandra
```

### 4.1 Baseline

1. Edit the `bin/cassandra` , comment Line 197 and uncomment Line 196.
2. Edit `conf/jvm11-server.options` and uncomment corresponding parameters to the configuration you want to test. And then start Cassandra. Wait until the script return before starting workloads.
    
    ```python
    # local_ratio: 100, 25, 13
    ./scripts/start-cassandra.sh baseline local_ratio
    ```
    
3. On zion-4, change to the cassandra scripts directory and run the workload:
    
    ```python
    cd /mnt/ssd/guest/scripts-repo/cassandra
    
    # workload: UI, RI, II
    # local_ratio: 100, 25, 13
    ./run_workload.sh baseline workload local_ratio
    ```
    
4. Stop Cassandra on zion-1 after running each workload and repeat from step 1.
    
    ```python
    ./bin/stop-server
    ```
    

### 4.2 MemLiner

1. Edit the `bin/cassandra` , comment Line 196 and uncomment Line 197.
2. Edit `conf/jvm11-server.options` and uncomment corresponding parameters to the configuration you want to test. Start Cassandra. Wait until the script return before starting workloads.
    
    ```python
    # local_ratio: 25, 13
    ./start_cassandra.sh memliner local_ratio
    ```
    
3. On zion-4, change to the cassandra scripts directory:
    
    ```python
    cd /mnt/ssd/guest/scripts-repo/cassandra
    
    # workload: UI, RI, II
    # local_ratio: 25, 13
    ./run_workload.sh memliner workload local_ratio
    ```
    
4. Stop Cassandra on zion-1 after running each workload and repeat from step 1.
    
    ```python
    ./bin/stop-server.sh
    ```
    

## 5. Running TradeSoap

Change to the tradesoap directory on zion-1:

```python
cd /mnt/ssd/guest/tradesoap
```

1. For baseline, run
    
    ```python
    # local_ratio: 100, 25, 13
    ./run_dacapo.sh baseline local_ratio
    ```
    
2. For MemLiner, run
    
    ```python
    # local_ratio: 25, 13
    ./run_dacapo.sh memliner local_ratio
    ```
    

You might encounter AxisFault error when running baseline or MemLiner. You can just ignore it because that’s tradesoap’s bug.

## 6. Running QuickCached

```bash
# on Zion-1
cd ~/quickcached
# start quickcached server
# tool: baseline, memliner
# local_ratio: 100, 25, 13
# app: qwd, qrd
./qcd.sh tool local_ratio app
# e.g., run QWD with baseline JVM under 25% local cache ratio
./qcd.sh baseline 25 qwd

# on Zion-4
cd ~/scripts-repo/memcached
# run QWD. and write log to ~/logs-qc/${logfile}.log
./ycsb-memcached-helper.sh memliner-quickcached-writedominant ${logfile}
# run QRD. and write log to ~/logs-qc/${logfile}.log
./ycsb-memcached-helper.sh memliner-quickcached-readdominant  ${logfile}

# on Zion-1
## use ctrl+C to kill Quickcached server
```

## 7. Results Visualization

To visualize the results, first we need to collect the execution time of applications and fill them in a few csv files. Then use a script to draw the figure.

### 7.1 Collect Execution Time

For Spark applications, when each application is run to the end on Zion-1, you can see the execution time formatted as below:

```
22/03/09 13:18:07 INFO ShutdownHookManager: Deleting directory ...
real 393.86
user 37.71
sys 2.79
```

The number after `real` is the execution time of the spark application, in seconds.

For Neo4j apps, find the log files under the logs folder in the command on Zion-1. And like Spark, in the end of the log file or stdout, the number after `real` is the execution time of the spark application, in seconds.

For Cassandra applications, on the ycsb client on Zion-4, under `/mnt/ssd/guest/scripts-repo/cassandra/logs`, you can find all the logs when running Cassandra apps. Select the run log, and you can see the run time of the app at the bottom of the file, as below:

```
2022-04-12 12:53:48:413 335 sec: 10000000 operations; ...
[OVERALL], RunTime(ms), 335107
[OVERALL], Throughput(ops/sec), 29841.214895540827
[TOTAL_GCS_G1_Young_Generation], Count, 8
```

The number after `[OVERALL], RunTime(ms),` is the execution time, in milliseconds.

The same information is printed in stdout when running the apps.

For Quickcached apps, the method is the same as Cassandra, except that the log folder is `/mnt/ssd/guest/logs-qc` on Zion-4.

For TradeSoap, after the application is finished on Zion-1, you can see the time as the output.

### 7.2 Generate the Plot.

On Zion-1. please change to this directory: `/mnt/ssd/guest/scripts-repo/projects/MemLiner/figures`, where you can find all data files and the plot script. First, fill in the execution time data (in seconds), to the three data file: `ae_baseline_time.csv`, `ae_memliner_time.csv`, and `ae_all_local_time.csv`. The order of data is denoted by the first row and the first column of the files. Then, run the script:

```
cd /mnt/ssd/guest/scripts-repo/projects/MemLiner/figures
python3 memliner_eval_ae.py
```

The figure should appear under the same directory named `evaluation51-ae.pdf`.
