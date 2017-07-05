
## Host network

* rc

```
# cat replication.yml 
apiVersion: v1
kind: ReplicationController
metadata:
  name: nginx
spec:
  replicas: 2
  selector:
    app: my-nginx
  template:
    metadata:
      name: nginx
      labels:
        app: my-nginx
    spec:
      hostNetwork: true
      containers:
      - name: nginx
        image: nginx:1.0
        ports:
        - containerPort: 80
```
  
* service

```
# cat service.yml 
{
    "kind": "Service",
    "apiVersion": "v1",
    "metadata": {
        "name": "my-nginx-service"
    },
    "spec": {
        "selector": {
            "app": "my-nginx"
        },
        "ports": [
            {
                "protocol": "TCP",
                "port": 8080,
                "targetPort": 80
            }
        ]
    }
}
```

iptables:

```
-A KUBE-PORTALS-CONTAINER -d 10.254.235.64/32 -p tcp -m comment --comment "default/my-nginx-service:" -m tcp --dport 8080 -j REDIRECT --to-ports 57697
-A KUBE-PORTALS-HOST -d 10.254.235.64/32 -p tcp -m comment --comment "default/my-nginx-service:" -m tcp --dport 8080 -j DNAT --to-destination 172.17.42.32:57697
```

其中，`172.17.42.32:57697`是本机kube-proxy的监听端口，VIP -> KUBE_PROXY->Nginx:

```sh
[root@kube-node2 ~]# telnet 10.254.235.64 8080

[root@kube-node2 ~]# ss -nt
ESTAB      0      0                                        172.17.42.32:53447                                   172.17.42.31:80
ESTAB      0      0                                 ::ffff:172.17.42.32:57697                            ::ffff:172.17.42.32:53012
```


## service loadbalancer

```
# kubectl label node kube-node3 role=loadbalancer

# cat nginx-haproxy.yml 
apiVersion: v1
kind: Pod
metadata:
  name: nginx-haproxy
spec:
  nodeSelector:
    role: loadbalancer
  containers:
  - image: hub.oa.com/google_containers/servicelb:1.6.5-rc1
    livenessProbe:
      httpGet:
        path: /healthz
        port: 8081
        scheme: HTTP
      initialDelaySeconds: 30
      timeoutSeconds: 5
    name: nginx-haproxy
    ports:
    - containerPort: 443
      protocol: TCP
    resources: {}
    args:
    - --tcp-services=nginx-service:443
    - --server=kube-master:8080
    - --use-kubernetes-cluster-service=false
    - --selectors=role:my-nginx
```


## performance

* [Fighting Service Latency in Microservices with Kubernetes](https://developers.redhat.com/blog/2017/05/18/fighting-service-latency-in-microservices-with-kubernetes/)
