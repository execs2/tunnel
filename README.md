##转发工具：

c语言写的端口转发工具<br>
主要用来实现访问内网机器，当然也可以作为跳板工具使用。<br>
写这个工具的原因是因为拉了N级运营商的宽带，没有外网ip，人在外面的时候无法访问家里的电脑。。。<br>
端口转发有很多现成的工具，但自己写好玩点<br>

ps:mem_pool.c 这个文件没用到，内存管理的代码是buffer.c<br>

##Usage:
1、首先要有台外网ip的机器<br>
2、在外网机器运行：tunnel -s port1[给内网机器连接的端口] prot2[给客户端连接的端口，比如secureCRT 这种ssh客户端]  如：tunnel -s 2222 3333<br>
3、在内网机器上运行：tunnel -c port1[要连接的本机服务端口，比如家里的ssh服务22端口] prot2[外网机器的监听的端口] 如：tunnel -c 22 2222<br>
4、任意机器连接外网的3333端口，发送的数据都将转发给内网机器22端口上（如secureCRT连接 外网ip:3333）<br>


##INSTALL:
```Bash
g++ -MMD -ggdb -Wall -lpthread -o tunnel  buffer.c socket_comm.c client.c server.c tunnel.c
```

##TODO:
```Bash
man accept
RETURN VALUE
  On success, these system calls return a non-negative integer that is  a  descriptor
  for the accepted socket.  On error, -1 is returned, and errno is set appropriately.
```
1.fd改成0开始。通过man accept文档知道，合法的socket fd应该是从0开始的。。。我的代码fd默认值0，并以此作为初始状态，用来判断某些状态，在某些极端情况会出错。<br>
2.和server建立连接后，server是等待客户端发包后再处理转发，但某些ssh客户端（如xshell）建立连接后在等待服务端返回数据前不发任何包，导致双方互相等待<br>
3.内存管理buffer还可以优化，目前是单buff全分配， 在回环前的时候，极端情况分配出来的内存太小，解决方案是加个最小内存块数，当环尾剩余内存块太小时，直接跳到环头再分配<br>

##我的邮箱：
  
646452100@qq.com
