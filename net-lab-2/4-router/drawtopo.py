import networkx as nx
import matplotlib.pyplot as plt

# 创建拓扑图
def draw_topology():
    # 初始化图
    topo = nx.Graph()

    # 添加节点
    hosts = ['h1', 'h2']
    routers = ['r1', 'r2', 'r3']
    topo.add_nodes_from(hosts, node_type='host', color='blue')
    topo.add_nodes_from(routers, node_type='router', color='red')

    # 添加链路
    links = [
        ('h1', 'r1'),
        ('r1', 'r2'),
        ('r2', 'r3'),
        ('r3', 'h2')
    ]
    topo.add_edges_from(links)

    # 设置节点颜色
    color_map = [data['color'] for _, data in topo.nodes(data=True)]

    # 绘制图
    plt.figure(figsize=(8, 6))
    pos = nx.spring_layout(topo, seed=42)  # 使用spring布局

    # 绘制节点和边
    nx.draw(topo, pos, with_labels=True, node_color=color_map, node_size=2000, font_size=12)
    
    # 添加节点类型标签
    legend_handles = [
        plt.Line2D([0], [0], marker='o', color='w', markerfacecolor='blue', markersize=10, label='Host'),
        plt.Line2D([0], [0], marker='o', color='w', markerfacecolor='red', markersize=10, label='Router')
    ]
    plt.legend(handles=legend_handles, loc='upper left')
    plt.title("Network Topology")
    plt.show()

if __name__ == "__main__":
    draw_topology()

