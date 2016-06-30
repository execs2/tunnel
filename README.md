##转发工具：

c语言写的端口转发工具
主要用来实现访问内网机器，当然也可以作为跳板工具使用。
写这个工具的原因是因为拉了N级运营商的宽带，没有外网ip，人在外面的时候无法访问家里的电脑。。。
端口转发有很多现成的工具，但自己写好玩点

ps:mem_pool.c 这个文件没用到，内存管理的代码是buffer.c

##RUN:
1、首先要有台外网ip的机器
2、在外网机器运行：tunnel -s port1[给内网机器连接的端口] prot2[给客户端连接的端口，比如secureCRT 这种ssh客户端]  如：tunnel -s 2222 3333
3、在内网机器上运行：tunnel -c port1[要连接的本机服务端口，比如家里的ssh服务22端口] prot2[外网机器的监听的端口] 如：tunnel -c 22 2222
4、任意机器连接外网的3333端口，发送的数据都将转发给内网机器22端口上（如secureCRT连接 外网ip:3333）


##INSTALL:
g++ -MMD -ggdb -Wall -lpthread -o tunnel  buffer.c socket_comm.c client.c server.c tunnel.c


##我的邮箱：
  
646452100@qq.com
