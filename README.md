### Xmqtt 是一个高性能 MQTT broker （仅支持MQTT 3.1.1） 

- 下载 编译  
 git clone https://github.com/Allenxuxu/Xmqtt.git  
 cd Xmqtt  
 cmake .  
 make  
- systemctl管理Xmqtt启动、停止、开机启动   
 sudo cp Xmqtt.service /etc/systemd/system/  
 sudo systemctl daemon-reload  
 sudo systemctl enable Xmqtt  
 sudo systemctl start Xmqtt  
 sudo systemctl status Xmqtt   --查看运行状态

