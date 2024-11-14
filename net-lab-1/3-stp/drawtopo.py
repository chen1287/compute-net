import networkx as nx
import matplotlib.pyplot as plt

# 创建一个图对象
G = nx.Graph()

# 添加节点 b1 到 b9
nodes = [f'b{i}' for i in range(1, 10)]
G.add_nodes_from(nodes)

# 添加链路（根据代码中的拓扑）
edges = [
    ('b1', 'b2'), ('b2', 'b3'), ('b3', 'b4'), ('b4', 'b5'),
    ('b5', 'b6'), ('b6', 'b7'), ('b7', 'b8'), ('b8', 'b9'),
    ('b9', 'b1'),  # 环形连接
    ('b1', 'b5'),  # 冗余链路1
    ('b2', 'b6'),  # 冗余链路2
    ('b3', 'b7'),  # 冗余链路3
]
G.add_edges_from(edges)

# 绘制图
pos = nx.spring_layout(G)  # 使用 spring layout 布局
nx.draw(G, pos, with_labels=True, node_size=700, node_color='skyblue', font_size=10, font_weight='bold', edge_color='gray')

# 显示图形
plt.title("Mininet Redundant Topology")
plt.show()

