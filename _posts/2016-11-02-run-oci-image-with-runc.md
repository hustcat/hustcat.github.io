---
layout: post
title: Run OCI image with runc
date: 2016-11-2 18:00:30
categories: Container
tags: runc oci
excerpt: Run OCI image with runc
---

* Download OCI image

Download [OCI image](https://github.com/opencontainers/image-spec) with [skopeo](https://github.com/projectatomic/skopeo).

```sh
# skopeo copy docker://busybox oci:busybox-oci
Getting image source signatures
Copying blob sha256:56bec22e355981d8ba0878c6c2f23b21f422f30ab0aba188b54f1ffeff59c190
 192.31 KB / 652.49 KB [================>--------------------------------------]
Copying config sha256:e02e811dd08fd49e7f6032625495118e63f597eb150403d02e3238af1df240ba
 0 B / 1.43 KB [---------------------------------------------------------------]
Writing manifest to image destination
Storing signatures

# ls busybox-oci/
blobs  oci-layout  refs

# tree busybox-oci/
busybox-oci/
|-- blobs
|   `-- sha256
|       |-- 56bec22e355981d8ba0878c6c2f23b21f422f30ab0aba188b54f1ffeff59c190
|       |-- d09bddf04324303fe923f8c2761041046fa08fec4e120b02f5900f450398df9b
|       `-- e02e811dd08fd49e7f6032625495118e63f597eb150403d02e3238af1df240ba
|-- oci-layout
`-- refs
    `-- latest
```

The [OCI image layout](https://github.com/opencontainers/image-spec/blob/master/image-layout.md) has two top level directories:

- "blobs" contains content-addressable blobs. A blob has no schema and should be considered opaque.
- "refs" contains [descriptors](https://github.com/opencontainers/image-spec/blob/master/descriptor.md). Commonly pointing to an [image manifest](https://github.com/opencontainers/image-spec/blob/master/manifest.md#image-manifest) or an [image manifest list](https://github.com/opencontainers/image-spec/blob/master/manifest-list.md#oci-image-manifest-list-specification).


`refs/latest` saved [OCI:Descriptor](https://github.com/opencontainers/image-spec/blob/master/specs-go/v1/descriptor.go#L18) data of `busybox:latest`:

```sh
# cat busybox-oci/refs/latest 
{"mediaType":"application/vnd.oci.image.manifest.v1+json","digest":"sha256:d09bddf04324303fe923f8c2761041046fa08fec4e120b02f5900f450
```

`blobs/sha256/d09bddf04324303fe923f8c2761041046fa08fec4e120b02f5900f450398df9b` save [OCI:Manifest](https://github.com/opencontainers/image-spec/blob/master/specs-go/v1/manifest.go#L20) data of [OCI image](https://github.com/opencontainers/image-spec/blob/master/manifest.md):

```sh
# cat busybox-oci/blobs/sha256/d09bddf04324303fe923f8c2761041046fa08fec4e120b02f5900f450398df9b  | python -mjson.tool
{
    "annotations": null, 
    "config": {
        "digest": "sha256:e02e811dd08fd49e7f6032625495118e63f597eb150403d02e3238af1df240ba", 
        "mediaType": "application/vnd.oci.image.config.v1+json", 
        "size": 1464
    }, 
    "layers": [
        {
            "digest": "sha256:56bec22e355981d8ba0878c6c2f23b21f422f30ab0aba188b54f1ffeff59c190", 
            "mediaType": "application/vnd.oci.image.layer.v1.tar+gzip", 
            "size": 668151
        }
    ], 
    "mediaType": "application/vnd.oci.image.manifest.v1+json", 
    "schemaVersion": 2
}
```

`blobs/sha256/e02e811dd08fd49e7f6032625495118e63f597eb150403d02e3238af1df240ba` save [OCI:Image](https://github.com/opencontainers/image-spec/blob/master/specs-go/v1/config.go#L78) data of [OCI image config](https://github.com/opencontainers/image-spec/blob/master/config.md):

```sh
# cat busybox-oci/blobs/sha256/e02e811dd08fd49e7f6032625495118e63f597eb150403d02e3238af1df240ba 
{"architecture":"amd64","config":{"Hostname":"4a74292706a0","Domainname":"","User":"","AttachStdin":false,"AttachStdout":false,"AttachStderr":false,"Tty":false,"OpenStdin":false,"StdinOnce":false,"Env":["PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"],"Cmd":["sh"],"Image":"sha256:1679bae2167496818312013654f5c66a16e185d0a0f6b762b53c8558014457c6","Volumes":null,"WorkingDir":"","Entrypoint":null,"OnBuild":null,"Labels":{}},"container":"8bb318a3b4672c53a1747991c95fff3306eea13ec308740ebe0c81b56ece530f","container_config":{"Hostname":"4a74292706a0","Domainname":"","User":"","AttachStdin":false,"AttachStdout":false,"AttachStderr":false,"Tty":false,"OpenStdin":false,"StdinOnce":false,"Env":["PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"],"Cmd":["/bin/sh","-c","#(nop) ","CMD [\"sh\"]"],"Image":"sha256:1679bae2167496818312013654f5c66a16e185d0a0f6b762b53c8558014457c6","Volumes":null,"WorkingDir":"","Entrypoint":null,"OnBuild":null,"Labels":{}},"created":"2016-10-07T21:03:58.469866982Z","docker_version":"1.12.1","history":[{"created":"2016-10-07T21:03:58.16783626Z","created_by":"/bin/sh -c #(nop) ADD file:ced3aa7577c8f970403004e45dd91e9240b1e3ee8bd109178822310bb5c4a4f7 in / "},{"created":"2016-10-07T21:03:58.469866982Z","created_by":"/bin/sh -c #(nop)  CMD [\"sh\"]","empty_layer":true}],"os":"linux","rootfs":{"type":"layers","diff_ids":["sha256:e88b3f82283bc59d5e0df427c824e9f95557e661fcb0ea15fb0fb6f97760f9d9"]}}
```

And one [layer tar gzip file](https://github.com/opencontainers/image-spec/blob/master/layer.md):

```sh
# file busybox-oci/blobs/sha256/56bec22e355981d8ba0878c6c2f23b21f422f30ab0aba188b54f1ffeff59c190 
busybox-oci/blobs/sha256/56bec22e355981d8ba0878c6c2f23b21f422f30ab0aba188b54f1ffeff59c190: gzip compressed data
```


## Create runc bundle

```sh
# mkdir mkdir busybox-bundle
# oci-create-runtime-bundle --ref latest busybox-oci busybox-bundle

# ls busybox-bundle/
config.json  rootfs

# tree -L 2 busybox-bundle/  
busybox-bundle/
|-- config.json
`-- rootfs
    |-- bin
    |-- dev
    |-- etc
    |-- home
    |-- root
    |-- tmp
    |-- usr
    `-- var
```

## Run container with runc

```sh
# cd busybox-bundle
# runc run busybox
sh-4.1# 
```

list container:

```sh
# runc list
ID          PID         STATUS      BUNDLE                           CREATED
busybox     27499       running     /data/dbyin/oci/busybox-bundle   2016-11-02T10:20:16.644759109Z

# runc state busybox
{
  "ociVersion": "1.0.0-rc2-dev",
  "id": "busybox",
  "pid": 27499,
  "status": "running",
  "bundle": "/data/dbyin/oci/busybox-bundle",
  "rootfs": "/data/dbyin/oci/busybox-bundle/rootfs",
  "created": "2016-11-02T10:20:16.644759109Z"
}
```

## Reference

* [OCI:runc](https://github.com/opencontainers/runc)
* [OCI:image-spec](https://github.com/opencontainers/image-spec)