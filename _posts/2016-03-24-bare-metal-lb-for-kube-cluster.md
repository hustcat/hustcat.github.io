---
layout: post
title: Setup Bare Metal Load Balancers for kubernetes service
date: 2016-03-24 10:00:30
categories: Container
tags: kubernetes
excerpt: Setup Bare Metal Load Balancers for kubernetes service
---

## Overview

There are 2 ways to expose a service to ingress traffic in the current kubernetes service model:

  (1) Create a cloud load balancer.

  (2) Allocate a port (the same port) on every node in your cluster and proxy ingress traffic through that port to the endpoints.



## Create service

Reference to [Services](http://kubernetes.io/docs/user-guide/services/).

## Create loadbalancer

Loadbalancers are created via a ReplicationController. Load balancers will only run on nodes with the role=loadbalancer label.

* manifest file

```sh
# cat rc.yaml         
apiVersion: v1
kind: ReplicationController
metadata:
  name: service-loadbalancer
  labels:
    app: service-loadbalancer
    version: v1
spec:
  replicas: 1
  selector:
    app: service-loadbalancer
    version: v1
  template:
    metadata:
      labels:
        app: service-loadbalancer
        version: v1
    spec:
      nodeSelector:
        role: loadbalancer
      containers:
      - image: gcr.io/google_containers/servicelb:0.4
        imagePullPolicy: Never
        livenessProbe:
          httpGet:
            path: /healthz
            port: 8081
            scheme: HTTP
          initialDelaySeconds: 30
          timeoutSeconds: 5
        name: haproxy
        ports:
        # All http services
        - containerPort: 80
          hostPort: 80
          protocol: TCP
        # nginx https
        - containerPort: 443
          hostPort: 8080
          protocol: TCP
        # mysql
        - containerPort: 3306
          hostPort: 3306
          protocol: TCP
        # haproxy stats
        - containerPort: 1936
          hostPort: 1936
          protocol: TCP
        resources: {}
        args:
        - --tcp-services=my-nginx-service:8080
          --server=172.17.42.31:443
```

* create RC

```sh
# kubectl create -f ./rc.yaml 
replicationcontroller "service-loadbalancer" created

# kubectl get rc -l app=service-loadbalancer
CONTROLLER             CONTAINER(S)   IMAGE(S)                                 SELECTOR                              REPLICAS   AGE
service-loadbalancer   haproxy        gcr.io/google_containers/servicelb:0.4   app=service-loadbalancer,version=v1   1          2m

# kubectl get pods -l app=service-loadbalancer
NAME                         READY     STATUS    RESTARTS   AGE
service-loadbalancer-mqv56   0/1       Pending   0          1m


# kubectl describe pods -l app=service-loadbalancer
Name:           service-loadbalancer-do0ix
Namespace:      default
Image(s):       gcr.io/google_containers/servicelb:0.4
Node:           /
Labels:         app=service-loadbalancer,version=v1
Status:         Pending
Reason:
Message:
IP:
Controllers:    ReplicationController/service-loadbalancer
Containers:
  haproxy:
    Container ID:
    Image:              gcr.io/google_containers/servicelb:0.4
    Image ID:
    Args:
      --tcp-services=mysql:3306,nginxsvc:443
    QoS Tier:
      cpu:              BestEffort
      memory:           BestEffort
    State:              Waiting
    Ready:              False
    Restart Count:      0
    Environment Variables:
Volumes:
  default-token-ekfrr:
    Type:       Secret (a secret that should populate this volume)
    SecretName: default-token-ekfrr
Events:
  FirstSeen     LastSeen        Count   From                    SubobjectPath   Type            Reason                  Message
  ---------     --------        -----   ----                    -------------   --------        ------                  -------
  5s            4s              2       {default-scheduler }                    Warning         FailedScheduling        pod (service-loadbalancer-do0ix) failed to fit in any node
fit failure on node (kube-node1): MatchNodeSelector
fit failure on node (kube-node3): MatchNodeSelector
fit failure on node (kube-node2): MatchNodeSelector

  2s    2s      1       {default-scheduler }            Warning FailedScheduling        pod (service-loadbalancer-do0ix) failed to fit in any node
fit failure on node (kube-node2): MatchNodeSelector
fit failure on node (kube-node1): MatchNodeSelector
fit failure on node (kube-node3): MatchNodeSelector
```

* set node as a load balancer

```sh
# kubectl label node kube-node3 role=loadbalancer
node "kube-node3" labeled

# kubectl get nodes                          
NAME         LABELS                                                STATUS    AGE
kube-node1   kubernetes.io/hostname=kube-node1                     Ready     29d
kube-node2   kubernetes.io/hostname=kube-node2                     Ready     29d
kube-node3   kubernetes.io/hostname=kube-node3,role=loadbalancer   Ready     3h

# kubectl describe pods -l app=service-loadbalancer
Name:           service-loadbalancer-kf1db
Namespace:      default
Image(s):       gcr.io/google_containers/servicelb:0.4
Node:           kube-node3/172.17.42.33
Start Time:     Thu, 17 Mar 2016 10:55:53 +0000
Labels:         app=service-loadbalancer,version=v1
Status:         Running
Reason:
Message:
IP:             172.16.86.4
Controllers:    ReplicationController/service-loadbalancer
Containers:
  haproxy:
    Container ID:       docker://6f8b823df363f74a503fb06801066b911659d5c5dd9b3fbfbd768029f045c83c
    Image:              gcr.io/google_containers/servicelb:0.4
    Image ID:           docker://2a88d37702442a587f45b8cec3ee87aa045f79c7a2f3413eceae70729070b661
    Args:
      --tcp-services=my-nginx-service:8080 --server=172.17.42.31:443
    QoS Tier:
      cpu:              BestEffort
      memory:           BestEffort
    State:              Running
      Started:          Thu, 17 Mar 2016 10:58:34 +0000
    Ready:              True
    Restart Count:      0
    Environment Variables:
Conditions:
  Type          Status
  Ready         True 
Volumes:
  default-token-ekfrr:
    Type:       Secret (a secret that should populate this volume)
    SecretName: default-token-ekfrr
No events.
```

* test

```sh
# curl http://172.16.86.4/my-nginx-service:8080
<!DOCTYPE html>
<html>
<head>
<title>Welcome to nginx!</title>
<style>
    body {
        width: 35em;
        margin: 0 auto;
        font-family: Tahoma, Verdana, Arial, sans-serif;
    }
</style>
</head>
<body>
<h1>Welcome to nginx!</h1>
<p>If you see this page, the nginx web server is successfully installed and
working. Further configuration is required.</p>

<p>For online documentation and support please refer to
<a href="http://nginx.org/">nginx.org</a>.<br/>
Commercial support is available at
<a href="http://nginx.com/">nginx.com</a>.</p>

<p><em>Thank you for using nginx.</em></p>
</body>
</html>
```

## Related posts
* [Bare Metal Service Load Balancers](https://github.com/kubernetes/contrib/tree/master/service-loadbalancer)
