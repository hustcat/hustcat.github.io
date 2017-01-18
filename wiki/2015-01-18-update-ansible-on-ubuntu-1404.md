---
layout: post
title: update ansible on ubuntu 14.04
date: 2016-01-18 18:00:30
categories: Linux
tags: other
excerpt: update ansible on ubuntu 14.04
---

# install

```sh
apt-get install software-properties-common
```

Add the Ansible repository to your system

```sh
$ sudo apt-add-repository ppa:ansible/ansible
```
Install Ansible

```sh
$ sudo apt-get update && sudo apt-get install ansible
```

# error

```
add-apt-repository ppa:ansible/ansible
Cannot add PPA: 'ppa:ansible/ansible'.
Please check that the PPA name or format is correct.
```

You need to export your proxy environment variables using

```
export http_proxy=http://username:password@host:port/
export https_proxy=https://username:password@host:port/
```

and then tell sudo to use them using:

```
	sudo -E add-apt-repository ppa:ansible/ansible
```

# reference

*[Install Ansible on 64-bit Ubuntu 14.04 LTS](https://community.spiceworks.com/how_to/110622-install-ansible-on-64-bit-ubuntu-14-04-lts)
*[Cannot Add PPA Behind Proxy Ubuntu 14.04 [duplicate]](http://askubuntu.com/questions/452595/cannot-add-ppa-behind-proxy-ubuntu-14-04)