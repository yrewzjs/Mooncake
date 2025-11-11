## 接口测试介绍
运行样例前需要在先编译安装mooncake，参考编译安装命令
```bash
cd Mooncake
apt-get update
apt-get install python3
bash dependencies.sh
apt purge mpich libmpich-dev
apt purge openmpi-bin
apt purge openmpi-bin libopenmpi-dev
apt install mpich libmpich-dev
export CPATH=/usr/lib/aarch64-linux-gnu/mpich/include/:$CPATH
export CPATH=/usr/lib/aarch64-linux-gnu/openmpi/lib:$CPATH
mkdir build
cd build
cmake ..
make -j
make instal
```

运行mooncake_master
```bash
mooncake_master   --enable_http_metadata_server=true   --http_metadata_server_host={ip}   --http_metadata_server_port=8080  --rpc_address={ip}
```

运行单测脚本
```python
python3 test_put_from_get_into.py
```
