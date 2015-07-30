---
layout: post
title: Run Ceph radosgw with civetweb
date: 2015-07-30 16:00:30
categories: Linux
tags: ceph
excerpt: Run Ceph radosgw with civetweb。
---

从Firefly开始，Ceph支持civetweb作为radosgw的前端web协议处理引擎，相比于apache方式，更加简单直接。

# 安装

```sh
#yum install ceph-radosgw -y
```

文件列表：

```sh
# rpm -ql ceph-radosgw
/etc/bash_completion.d/radosgw-admin
/etc/rc.d/init.d/ceph-radosgw
/usr/bin/radosgw
/usr/bin/radosgw-admin
/usr/sbin/rcceph-radosgw
/usr/share/man/man8/radosgw-admin.8.gz
/usr/share/man/man8/radosgw.8.gz
/var/log/radosgw
```

# 创建user和keyring

- Create keyring file

```sh
# ceph-authtool --create-keyring /etc/ceph/ceph.client.radosgw.keyring
creating /etc/ceph/ceph.client.radosgw.keyring
# cat /etc/ceph/ceph.client.radosgw.keyring
# ceph-authtool /etc/ceph/ceph.client.radosgw.keyring -n client.radosgw.gateway --gen-key
# cat /etc/ceph/ceph.client.radosgw.keyring                                              
[client.radosgw.gateway]
        key = AQACp7lV8EuEDRAAPNCjv2MgavuPxq355W21Ug==
```

- Add capabilities

```sh
# ceph-authtool -n client.radosgw.gateway --cap osd 'allow rwx' --cap mon 'allow rw' /etc/ceph/ceph.client.radosgw.keyring
# cat /etc/ceph/ceph.client.radosgw.keyring
[client.radosgw.gateway]
        key = AQACp7lV8EuEDRAAPNCjv2MgavuPxq355W21Ug==
        caps mon = "allow rw"
        caps osd = "allow rwx"
```

- Add key to cluster

```sh
# ceph -k /etc/ceph/ceph.client.admin.keyring auth add client.radosgw.gateway -i /etc/ceph/ceph.client.radosgw.keyring
added key for client.radosgw.gateway
# ceph auth list
client.radosgw.gateway
        key: AQACp7lV8EuEDRAAPNCjv2MgavuPxq355W21Ug==
        caps: [mon] allow rw
        caps: [osd] allow rwx
```

将keyring文件/etc/ceph/ceph.client.radosgw.keyring拷贝到radosgw节点。

# 修改ceph配置文件

配置文件radosgw节点的/etc/ceph/ceph.conf，增加：

```
[client.radosgw.gateway]
host = host2
keyring = /etc/ceph/ceph.client.radosgw.keyring
rgw_socket_path = /tmp/radosgw.sock
log_file = /var/log/radosgw/radosgw.log
rgw frontends = "civetweb port=80"
```

其中，host2为运行radosgw的hostname。并使用radosgw自带的web client civetweb。

# Run radosgw

```sh
# radosgw --id=radosgw.gateway
# radosgw -c /etc/ceph/ceph.conf --keyring=/etc/ceph/ceph.client.radosgw.keyring --id=radosgw.gateway -d
```

# Use radosgw

* Create radosgw user for S3 access

```sh
# radosgw-admin user create --uid=dbyin --display-name="Yin Ye" --email=dbyin@tencent.com
{ "user_id": "dbyin",
  "display_name": "Yin Ye",
  "email": "dbyin@tencent.com",
  "suspended": 0,
  "max_buckets": 1000,
  "auid": 0,
  "subusers": [],
  "keys": [
        { "user": "dbyin",
          "access_key": "PC8URMEKC3IO1VS752KF",
          "secret_key": "gmcECdJiPkLQU07Ljm\/iaWDQaYeLvkWyQAB+EBh3"}],
  "swift_keys": [],
  "caps": [],
  "op_mask": "read, write, delete",
  "default_placement": "",
  "placement_tags": [],
  "bucket_quota": { "enabled": false,
      "max_size_kb": -1,
      "max_objects": -1},
  "user_quota": { "enabled": false,
      "max_size_kb": -1,
      "max_objects": -1},
  "temp_url_keys": []}
```

* Test S3 acess

```sh
# yum install python-boto
```

Python script:

```
# cat tests3.py 
import boto
import boto.s3.connection
access_key = 'PC8URMEKC3IO1VS752KF'
secret_key = 'gmcECdJiPkLQU07Ljm/iaWDQaYeLvkWyQAB+EBh3'

conn = boto.connect_s3(
        aws_access_key_id = access_key,
        aws_secret_access_key = secret_key,
        host = 'host2',
        is_secure=False,
        calling_format = boto.s3.connection.OrdinaryCallingFormat(),
        )
bucket = conn.create_bucket('my-new-bucket')
for bucket in conn.get_all_buckets():
        print "{name}\t{created}".format(
                name = bucket.name,
                created = bucket.creation_date,
        )
```

Run python script

```sh
# python tests3.py       
my-new-bucket   2015-07-30T07:07:50.000Z
```

# radosgw相关的pool：

```sh
# rados lspools
.rgw.root
.rgw.control
.rgw
.rgw.gc
.users.uid
.users.email
.users
.rgw.buckets.index

# rados -p .users.uid ls
dbyin.buckets
dbyin
# rados -p .users ls
PC8URMEKC3IO1VS752KF
# rados -p .rgw ls              
.bucket.meta.my-new-bucket:default.4709.1
my-new-bucket
```

# 参考资料

* http://docs.ceph.com/docs/master/radosgw/config/
* http://docs.ceph.com/docs/master/man/8/radosgw/
