#!/usr/bin/python

from __future__ import print_function
import os
import sys
import glob

from mininet.topo import Topo
from mininet.net import Mininet
from mininet.link import TCLink
from mininet.cli import CLI

# 脚本依赖
script_deps = ['ethtool']

def check_scripts():
    dir = os.path.abspath(os.path.dirname(sys.argv[0]))

    for fname in glob.glob(dir + '/' + 'scripts/*.sh'):
        if not os.access(fname, os.X_OK):
            print('%s should be set executable by using `chmod +x $script_name`' % (fname))
            sys.exit(1)

    for program in script_deps:
        found = False
        for path in os.environ['PATH'].split(os.pathsep):
            exe_file = os.path.join(path, program)
            if os.path.isfile(exe_file) and os.access(exe_file, os.X_OK):
                found = True
                break
        if not found:
            print('`%s` is required but missing. Please install it via `apt` or `aptitude`.' % (program))
            sys.exit(2)

# 清除交换机节点的 IP 地址配置
def clearIP(n):
    for iface in n.intfList():
        n.cmd('ifconfig %s 0.0.0.0' % (iface))

# 定义自定义拓扑
class RedundantTopo(Topo):
    def build(self):
        # 添加主机节点
        h1 = self.addHost('h1')
        h2 = self.addHost('h2')
        h3 = self.addHost('h3')

        # 添加交换机节点
        s1 = self.addSwitch('s1')
        s2 = self.addSwitch('s2')
        s3 = self.addSwitch('s3')
        s4 = self.addSwitch('s4')

        # 链接主机到交换机
        self.addLink(h1, s1, bw=20)
        self.addLink(h2, s2, bw=10)
        self.addLink(h3, s3, bw=10)

        # 添加交换机之间的链路
        # s1 <-> s3 (冗余链路)
        self.addLink(s1, s3, bw=15)
        
        # s2 <-> s4 (冗余链路)
        self.addLink(s2, s4, bw=15)
        
        # s1 <-> s2
        self.addLink(s1, s2, bw=10)

        # s3 <-> s4
        self.addLink(s3, s4, bw=10)

        # s1 <-> s4 (另一条冗余链路)
        self.addLink(s1, s4, bw=10)

if __name__ == '__main__':
    check_scripts()

    # 初始化拓扑
    topo = RedundantTopo()
    net = Mininet(topo=topo, link=TCLink, controller=None)

    # 获取网络节点
    h1, h2, h3 = net.get('h1', 'h2', 'h3')
    s1, s2, s3, s4 = net.get('s1', 's2', 's3', 's4')

    # 配置主机 IP 地址
    h1.cmd('ifconfig h1-eth0 10.0.0.1/8')
    h2.cmd('ifconfig h2-eth0 10.0.0.2/8')
    h3.cmd('ifconfig h3-eth0 10.0.0.3/8')

    # 清除交换机的 IP 配置
    for switch in [s1, s2, s3, s4]:
        clearIP(switch)

    # 禁用所有节点的网卡 offload 和 IPv6
    for node in [h1, h2, h3, s1, s2, s3, s4]:
        node.cmd('./scripts/disable_offloading.sh')
        node.cmd('./scripts/disable_ipv6.sh')

    # 启动网络
    net.start()
    CLI(net)
    net.stop()
