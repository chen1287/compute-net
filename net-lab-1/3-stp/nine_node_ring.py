#!/usr/bin/python

import os
import sys
import glob

from mininet.topo import Topo
from mininet.net import Mininet
from mininet.cli import CLI

script_deps = [ 'ethtool' ]

def check_scripts():
    dir = os.path.abspath(os.path.dirname(sys.argv[0]))
    
    for fname in glob.glob(dir + '/' + 'scripts/*.sh'):
        if not os.access(fname, os.X_OK):
            print('%s should be set executable by using chmod +x $script_name' % (fname))
            sys.exit(1)

    for program in script_deps:
        found = False
        for path in os.environ['PATH'].split(os.pathsep):
            exe_file = os.path.join(path, program)
            if os.path.isfile(exe_file) and os.access(exe_file, os.X_OK):
                found = True
                break
        if not found:
            print('%s is required but missing, which could be installed via apt or aptitude' % (program))
            sys.exit(2)

def clearIP(n):
    for iface in n.intfList():
        n.cmd('ifconfig %s 0.0.0.0' % (iface))

class RedundantTopo(Topo):
    def build(self):
        # 创建9个节点
        nodes = []
        for i in range(9):
            nodes.append(self.addHost(f'b{i+1}'))

        # 创建冗余链路（3条）
        self.addLink(nodes[0], nodes[1])
        self.addLink(nodes[1], nodes[2])
        self.addLink(nodes[2], nodes[3])
        self.addLink(nodes[3], nodes[4])
        self.addLink(nodes[4], nodes[5])
        self.addLink(nodes[5], nodes[6])
        self.addLink(nodes[6], nodes[7])
        self.addLink(nodes[7], nodes[8])
        self.addLink(nodes[8], nodes[0])  # 冗余链路1

        self.addLink(nodes[0], nodes[4])  # 冗余链路2
        self.addLink(nodes[1], nodes[5])  # 冗余链路3

if __name__ == '__main__':
    check_scripts()

    topo = RedundantTopo()
    net = Mininet(topo=topo, controller=None)

    for idx in range(9):
        name = f'b{idx+1}'
        node = net.get(name)
        clearIP(node)
        node.cmd('./scripts/disable_offloading.sh')
        node.cmd('./scripts/disable_ipv6.sh')
        node.cmd('./stp > %s-output.txt 2>&1 &' % name)

        # 设置每个接口的 MAC 地址
        for port in range(len(node.intfList())):
            intf = f'{name}-eth{port}'
            mac = f'00:00:00:00:0{idx+1}:0{port+1}'

            node.setMAC(mac, intf=intf)

        # node.cmd('./stp > %s-output.txt 2>&1 &' % name)
        # node.cmd('./stp-reference > %s-output.txt 2>&1 &' % name)

    net.start()
    CLI(net)
    net.stop()
