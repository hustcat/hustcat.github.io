---
layout: post
title: The new stored format of Docker image on disk and Distribution
date: 2016-10-25 20:00:30
categories: Container
tags: docker
excerpt: The new stored format of Docker image on disk and Distribution
---

## 1 Introduction

自从Docker v1.10之后，Docker镜像在存储格式发生了很大的变化。主要是为了解决之前版本的镜像的一些问题：

(1)安全性。v1.10之前的image ID是随机生成的，与内容无关。这一方面很容易导致image ID被伪造，也容易引发冲突;

(2)image与layer没有严格的区别。每个layer都对应一个image ID和配置文件。这一方面导致image与layer的概念很模糊；另一方面，数据重复，考虑两个数据完全相同的layer，却是两个不同的layer。

为了解决上面的问题，新的格式下，image与layer是严格区分的。image ID与layer ID是两个不同的对象，生成的算法也不一样，但都是根据内容(content-addressable)生成的。正是这个区别导致了新的镜像存储在本地磁盘和registry端都发生了很大的变化。

## 2 Content hashes vs. distribution hashes

`distribution`存储的layer是压缩文件，所以使用`hashes of compressed data`；而Docker在本地使用`content hashes`，即`hashes of uncompressed data`。为什么要做这种区分？

首先，对于registry，使用`hashes of compressed data`，维护简单，而且Docker在下载时，不需要解压就可以验证文件。但是，每个push生成的`distribution hashes`可能会不一样，因为同一个tar文件，每次压缩生成的文件可能会有区别。

另一方面，对于`docker build`、`docker save`和`docker load`，`uncompressed tars`更加适合，所以本地使用`hashes of uncompressed data`。

[aaronlehmann](https://github.com/aaronlehmann)在[这里](https://gist.github.com/aaronlehmann/b42a2eaf633fc949f93)详细讨论了这些问题。

## 3 Distribution format

`distribution`顶层有2个目录，`repositories`存储image/tag相关的信息，blobs目录存储实际的layer数据:

```sh
# ls
blobs  repositories
```

以`busybox:latest`为例：

```sh
# docker pull busybox:latest
latest: Pulling from busybox
c0a04912aa5a: Pull complete 
a3ed95caeb02: Pull complete 
Digest: sha256:ad28941632ea14c85dcac9fd2171599cd57381b40b4aac75f9ad67a3636d6658
Status: Downloaded newer image for busybox:latest

# docker images
REPOSITORY                  TAG                 IMAGE ID            CREATED             SIZE
busybox                    latest              9e301a362a27        13 months ago       364.3 MB
```

可以看到，`busybox:latest`有2个layer：`c0a04912aa5a`和`a3ed95caeb02`为layer的`distribution hash`；`sha256:ad28941632ea14c85dcac9fd2171599cd57381b40b4aac75f9ad67a3636d6658`为tag的digest；`9e301a362a27`为image ID。我们来看看这4个hash值在distribution端的存储情况。

* tag digest

```sh
# ls repositories/busybox/_manifests/tags/latest/index/sha256/
ad28941632ea14c85dcac9fd2171599cd57381b40b4aac75f9ad67a3636d6658
```

* repository's layers

`busybox`的所有layer:

```sh
# ls repositories/busybox/_layers/sha256/
9e301a362a270bcb6900ebd1aad1b3a9553a9d055830bdf4cab5c2184187a2d1  c0a04912aa5afc0b4fd4c34390e526d547e67431f6bc122084f1e692dcb7d34e
a3ed95caeb02ffe68cdd9fd84406680ae93d633cb16422d00e8a7c22955b46d4
```

其中，`9e301a362a270bcb6900ebd1aad1b3a9553a9d055830bdf4cab5c2184187a2d1`对应image ID。

* layer data

```sh
# ls blobs/ -R
blobs/:
sha256

blobs/sha256:
9e  a3  ad  c0

blobs/sha256/9e:
9e301a362a270bcb6900ebd1aad1b3a9553a9d055830bdf4cab5c2184187a2d1

blobs/sha256/9e/9e301a362a270bcb6900ebd1aad1b3a9553a9d055830bdf4cab5c2184187a2d1:
data

blobs/sha256/a3:
a3ed95caeb02ffe68cdd9fd84406680ae93d633cb16422d00e8a7c22955b46d4

blobs/sha256/a3/a3ed95caeb02ffe68cdd9fd84406680ae93d633cb16422d00e8a7c22955b46d4:
data

blobs/sha256/ad:
ad28941632ea14c85dcac9fd2171599cd57381b40b4aac75f9ad67a3636d6658

blobs/sha256/ad/ad28941632ea14c85dcac9fd2171599cd57381b40b4aac75f9ad67a3636d6658:
data

blobs/sha256/c0:
c0a04912aa5afc0b4fd4c34390e526d547e67431f6bc122084f1e692dcb7d34e

blobs/sha256/c0/c0a04912aa5afc0b4fd4c34390e526d547e67431f6bc122084f1e692dcb7d34e:
data
```

可以看到，多了一个`ad28941632ea14c85dcac9fd2171599cd57381b40b4aac75f9ad67a3636d6658`的目录，保存了tag->(image ID, layers)的信息：

```sh
# cat blobs/sha256/ad/ad28941632ea14c85dcac9fd2171599cd57381b40b4aac75f9ad67a3636d6658/data  
{
   "schemaVersion": 2,
   "mediaType": "application/vnd.docker.distribution.manifest.v2+json",
   "config": {
      "mediaType": "application/vnd.docker.container.image.v1+json",
      "size": 1374,
      "digest": "sha256:9e301a362a270bcb6900ebd1aad1b3a9553a9d055830bdf4cab5c2184187a2d1"
   },
   "layers": [
      {
         "mediaType": "application/vnd.docker.image.rootfs.diff.tar.gzip",
         "size": 224153958,
         "digest": "sha256:c0a04912aa5afc0b4fd4c34390e526d547e67431f6bc122084f1e692dcb7d34e"
      },
      {
         "mediaType": "application/vnd.docker.image.rootfs.diff.tar.gzip",
         "size": 32,
         "digest": "sha256:a3ed95caeb02ffe68cdd9fd84406680ae93d633cb16422d00e8a7c22955b46d4"
      }
   ]
}
```
从这个目录，可以得到tag对应的image ID，以及image所有的layer。

另外，`9e301a362a270bcb6900ebd1aad1b3a9553a9d055830bdf4cab5c2184187a2d1`目录保存了image的信息：

```sh
# cat blobs/sha256/9e/9e301a362a270bcb6900ebd1aad1b3a9553a9d055830bdf4cab5c2184187a2d1/data | python -mjson.tool
{
    "architecture": "amd64", 
    "config": {
        "AttachStderr": false, 
        "AttachStdin": false, 
        "AttachStdout": false, 
        "Cmd": [
            "sh"
        ], 
        "Domainname": "", 
        "Entrypoint": null, 
        "Env": null, 
        "Hostname": "5f8e0e129ff1", 
        "Image": "cfa753dfea5e68a24366dfba16e6edf573daa447abf65bc11619c1a98a3aff54", 
        "Labels": null, 
        "OnBuild": null, 
        "OpenStdin": false, 
        "StdinOnce": false, 
        "Tty": false, 
        "User": "", 
        "Volumes": null, 
        "WorkingDir": ""
    }, 
    "container": "7f652467f9e6d1b3bf51172868b9b0c2fa1c711b112f4e987029b1624dd6295f", 
    "container_config": {
        "AttachStderr": false, 
        "AttachStdin": false, 
        "AttachStdout": false, 
        "Cmd": [
            "/bin/sh", 
            "-c", 
            "#(nop) CMD [\"sh\"]"
        ], 
        "Domainname": "", 
        "Entrypoint": null, 
        "Env": null, 
        "Hostname": "5f8e0e129ff1", 
        "Image": "cfa753dfea5e68a24366dfba16e6edf573daa447abf65bc11619c1a98a3aff54", 
        "Labels": null, 
        "OnBuild": null, 
        "OpenStdin": false, 
        "StdinOnce": false, 
        "Tty": false, 
        "User": "", 
        "Volumes": null, 
        "WorkingDir": ""
    }, 
    "created": "2015-09-21T20:15:47.866196515Z", 
    "docker_version": "1.8.2", 
    "history": [
        {
            "created": "2015-09-21T20:15:47.433616227Z", 
            "created_by": "/bin/sh -c #(nop) ADD file:6cccb5f0a3b3947116a0c0f55d071980d94427ba0d6dad17bc68ead832cc0a8f in /"
        }, 
        {
            "created": "2015-09-21T20:15:47.866196515Z", 
            "created_by": "/bin/sh -c #(nop) CMD [\"sh\"]"
        }
    ], 
    "os": "linux", 
    "rootfs": {
        "diff_ids": [
            "sha256:ae2b342b32f9ee27f0196ba59e9952c00e016836a11921ebc8baaf783847686a", 
            "sha256:5f70bf18a086007016e948b04aed3b82103a36bea41755b6cddfaf10ace3c6ef"
        ], 
        "type": "layers"
    }
}
```

另外两个目录为layer tar data。

## 4 Pulling An Image

### 4.1 Pulling an Image Manifest

当docker拉取image时，首先会通过[Registry API](https://github.com/docker/distribution/blob/master/docs/spec/api.md)获取manifest信息：

```
GET /v2/<name>/manifests/<reference>
```

> The name and reference parameter identify the image and are required. The reference may include a tag or digest.


* [Image Manifest Version 2, Schema 1](https://github.com/docker/distribution/blob/master/docs/spec/manifest-v2-1.md)

```sh
# curl -L "http://10.x.x.x:5000/v2/busybox/manifests/latest"
{
   "schemaVersion": 1,
   "name": "busybox",
   "tag": "latest",
   "architecture": "amd64",
   "fsLayers": [
      {
         "blobSum": "sha256:a3ed95caeb02ffe68cdd9fd84406680ae93d633cb16422d00e8a7c22955b46d4"
      },
      {
         "blobSum": "sha256:c0a04912aa5afc0b4fd4c34390e526d547e67431f6bc122084f1e692dcb7d34e"
      }
   ],
   "history": [
      {
         "v1Compatibility": "{\"architecture\":\"amd64\",\"config\":{\"Hostname\":\"5f8e0e129ff1\",\"Domainname\":\"\",\"User\":\"\",\"AttachStdin\":false,\"AttachStdout\":false,\"AttachStderr\":false,\"Tty\":false,\"OpenStdin\":false,\"StdinOnce\":false,\"Env\":null,\"Cmd\":[\"sh\"],\"Image\":\"cfa753dfea5e68a24366dfba16e6edf573daa447abf65bc11619c1a98a3aff54\",\"Volumes\":null,\"WorkingDir\":\"\",\"Entrypoint\":null,\"OnBuild\":null,\"Labels\":null},\"container\":\"7f652467f9e6d1b3bf51172868b9b0c2fa1c711b112f4e987029b1624dd6295f\",\"container_config\":{\"Hostname\":\"5f8e0e129ff1\",\"Domainname\":\"\",\"User\":\"\",\"AttachStdin\":false,\"AttachStdout\":false,\"AttachStderr\":false,\"Tty\":false,\"OpenStdin\":false,\"StdinOnce\":false,\"Env\":null,\"Cmd\":[\"/bin/sh\",\"-c\",\"#(nop) CMD [\\\"sh\\\"]\"],\"Image\":\"cfa753dfea5e68a24366dfba16e6edf573daa447abf65bc11619c1a98a3aff54\",\"Volumes\":null,\"WorkingDir\":\"\",\"Entrypoint\":null,\"OnBuild\":null,\"Labels\":null},\"created\":\"2015-09-21T20:15:47.866196515Z\",\"docker_version\":\"1.8.2\",\"id\":\"bc315f8368c625be080de23e37c18bd7e555787987f907ccaf0b9bd1b69d5fe8\",\"os\":\"linux\",\"parent\":\"77106241d10a8ed96dc42f690453f89ec95890b1494ccac18bac69065e4b67fa\"}"
      },
      {
         "v1Compatibility": "{\"id\":\"77106241d10a8ed96dc42f690453f89ec95890b1494ccac18bac69065e4b67fa\",\"created\":\"2015-09-21T20:15:47.433616227Z\",\"container_config\":{\"Cmd\":[\"/bin/sh -c #(nop) ADD file:6cccb5f0a3b3947116a0c0f55d071980d94427ba0d6dad17bc68ead832cc0a8f in /\"]}}"
      }
   ],
   "signatures": [
      {
         "header": {
            "jwk": {
               "crv": "P-256",
               "kid": "LOC3:5XPT:EA5R:3TC3:BJT6:I3DM:FREB:PMXJ:JOPP:PCFT:L2IF:5N3H",
               "kty": "EC",
               "x": "FEMMDfyxC4WwfbsFe9qeCuFeK5QwgAxRYazrp1HgAQc",
               "y": "EnOzI1hTixYK8g2_gklO2Zkvb7bP5gzCkEG_v-aOqx0"
            },
            "alg": "ES256"
         },
         "signature": "bZtcwb6n0cW4Ch9L3mpmD6HJX29CE7f7rdQPIEHlxlhX_6qCUK0qS6dIaM8yWlQPr7cQrAieINTWMc5cj898Eg",
         "protected": "eyJmb3JtYXRMZW5ndGgiOjE5MTgsImZvcm1hdFRhaWwiOiJDbjAiLCJ0aW1lIjoiMjAxNi0xMC0yNVQwMjo1ODo1N1oifQ"
      }
   ]
}
```

* [Image Manifest Version 2, Schema 2](https://github.com/docker/distribution/blob/master/docs/spec/manifest-v2-2.md)

```sh
# curl -L "http://10.x.x.x:5000/v2/busybox/manifests/sha256:ad28941632ea14c85dcac9fd2171599cd57381b40b4aac75f9ad67a3636d6658"
{
   "schemaVersion": 2,
   "mediaType": "application/vnd.docker.distribution.manifest.v2+json",
   "config": {
      "mediaType": "application/vnd.docker.container.image.v1+json",
      "size": 1374,
      "digest": "sha256:9e301a362a270bcb6900ebd1aad1b3a9553a9d055830bdf4cab5c2184187a2d1"
   },
   "layers": [
      {
         "mediaType": "application/vnd.docker.image.rootfs.diff.tar.gzip",
         "size": 224153958,
         "digest": "sha256:c0a04912aa5afc0b4fd4c34390e526d547e67431f6bc122084f1e692dcb7d34e"
      },
      {
         "mediaType": "application/vnd.docker.image.rootfs.diff.tar.gzip",
         "size": 32,
         "digest": "sha256:a3ed95caeb02ffe68cdd9fd84406680ae93d633cb16422d00e8a7c22955b46d4"
      }
   ]
}
```

### 4.2 Pulling a Layer

对应的接口为:

```
GET /v2/<name>/blobs/<digest>
```

> Access to a layer will be gated by the name of the repository but is identified uniquely in the registry by digest.

示例：

```sh
# curl -L --progress "http://10.x.x.x:5000/v2/busybox/blobs/sha256:c0a04912aa5afc0b4fd4c34390e526d547e67431f6bc122084f1e692dcb7d34e" -o layer.tar 
######################################################################## 100.0%
# sha256sum layer.tar 
c0a04912aa5afc0b4fd4c34390e526d547e67431f6bc122084f1e692dcb7d34e  layer.tar
```

## 5 On-disk format

### 5.1 ID definitions and calculations

在讨论镜像在本地的存储之前，需要先理解几个ID及其对应的计算方法：`layer.DiffID`、`layer.ChainID`和`image.ID`:

ID scheme     | Meaning | Calculation
------------- | ------- | -----------
`layer.DiffID` | ID for an individual layer | `DiffID = SHA256hex(uncompressed layer tar data)`
`layer.ChainID`  | ID for a layer and its parents. This ID uniquely identifies a filesystem composed of a set of layers. | For bottom layer: `ChainID(layer0) = DiffID(layer0)`
 | | For other layers: `ChainID(layerN) = SHA256hex(ChainID(layerN-1) + " " + DiffID(layerN))`
`image.ID` | ID for an image. Since the image configuration references the layers the image uses, this ID incorporates the filesystem data and the rest of the image configuration. | `SHA256hex(imageConfigJSON)`
"V1 ID1" | legacy image/layer ID; originally not content-addressable | Calculated for schema1 manifest:
 | | For top layer: `V1ID(layerTOP) = SHA256hex(blobsum(TOP) + " " + V1ID(layerTOP-1) + " " + imageConfigJSON)`
 | | For other layers: `V1ID(layerN) = SHA256hex(blobsum(layerN) + " " + V1ID(layerN-1))`

详细参考[Docker Image Specification v1.2.0](https://github.com/docker/docker/blob/master/image/spec/v1.2.md)和[ID definitions and calculations](https://gist.github.com/aaronlehmann/b42a2eaf633fc949f93b#id-definitions-and-calculations)。

示例：

```sh
# docker inspect 9e301a362a27
[
    {
        "Id": "sha256:9e301a362a270bcb6900ebd1aad1b3a9553a9d055830bdf4cab5c2184187a2d1",  ### image ID
        "RepoTags": [
            "busybox:latest"
        ],
        "RepoDigests": [ ### tag digest
            "busybox@sha256:ad28941632ea14c85dcac9fd2171599cd57381b40b4aac75f9ad67a3636d6658"
        ],
        "Parent": "",
        "Comment": "",
        "Created": "2015-09-21T20:15:47.866196515Z",
        "Size": 364348077,
        "VirtualSize": 364348077,
        "GraphDriver": {
            "Name": "overlay",
            "Data": {
                "RootDir": "/data/docker/overlay/390c9a37d1edca218961b4133c67992b6e62b1b1c1cdf820494ff4df5b9fce7c/root"
            }
        },
        "RootFS": {
            "Type": "layers",
            "Layers": [  ###diff IDs
                "sha256:ae2b342b32f9ee27f0196ba59e9952c00e016836a11921ebc8baaf783847686a", 
                "sha256:5f70bf18a086007016e948b04aed3b82103a36bea41755b6cddfaf10ace3c6ef"
            ]
        }
    }
]
```

* Image ID

Image config -> Image ID

```go
func (p *v2Puller) pullSchema1(ctx context.Context, ref reference.Named, unverifiedManifest *schema1.SignedManifest) (imageID image.ID, manifestDigest digest.Digest, err error) {

    config, err := v1.MakeConfigFromV1Config([]byte(verifiedManifest.History[0].V1Compatibility), &resultRootFS, history)
    if err != nil {
        return "", "", err
    }

    imageID, err = p.config.ImageStore.Create(config) ///image ID
    if err != nil {
        return "", "", err
    }
}

///image/store.go
func (is *store) Create(config []byte) (ID, error) { ///image config -> image ID
    dgst, err := is.fs.Set(config)

    imageID := ID(dgst)
}
```

* layer.DiffID

```go
///layer/layer_store.go
///ts: tar gzip file; parent: 上一层layer.ChainID
func (ls *layerStore) Register(ts io.Reader, parent ChainID) (Layer, error) {
    ///解压layer，同时计算DiffID
    if err = ls.applyTar(tx, ts, pid, layer); err != nil {
        return nil, err
    }
}

/// 解压的同时,计算diffID
func (ls *layerStore) applyTar(tx MetadataTransaction, ts io.Reader, parent string, layer *roLayer) error {
    digester := digest.Canonical.New() ///计算DiffID
    tr := io.TeeReader(ts, digester.Hash())

    tsw, err := tx.TarSplitWriter(true)
    if err != nil {
        return err
    }
    metaPacker := storage.NewJSONPacker(tsw)
    defer tsw.Close()

    // we're passing nil here for the file putter, because the ApplyDiff will
    // handle the extraction of the archive
    rdr, err := asm.NewInputTarStream(tr, metaPacker, nil)
    if err != nil {
        return err
    }

    applySize, err := ls.driver.ApplyDiff(layer.cacheID, parent, archive.Reader(rdr))
    if err != nil {
        return err
    }

    // Discard trailing data but ensure metadata is picked up to reconstruct stream
    io.Copy(ioutil.Discard, rdr) // ignore error as reader may be closed

    layer.size = applySize
    layer.diffID = DiffID(digester.Digest())
}
```

* layer.ChainID

```go
///image/rootfs_unix.go
// RootFS describes images root filesystem
// This is currently a placeholder that only supports layers. In the future
// this can be made into an interface that supports different implementations.
type RootFS struct {
    Type    string         `json:"type"`
    DiffIDs []layer.DiffID `json:"diff_ids,omitempty"`
}

// ChainID returns the ChainID for the top layer in RootFS.
func (r *RootFS) ChainID() layer.ChainID {
    return layer.CreateChainID(r.DiffIDs)
}


///layer/layer.go
// CreateChainID returns ID for a layerDigest slice
func CreateChainID(dgsts []DiffID) ChainID {
    return createChainIDFromParent("", dgsts...)
}

func createChainIDFromParent(parent ChainID, dgsts ...DiffID) ChainID {
    if len(dgsts) == 0 {
        return parent
    }
    if parent == "" {
        return createChainIDFromParent(ChainID(dgsts[0]), dgsts[1:]...)
    }
    // H = "H(n-1) SHA256(n)"
    dgst := digest.FromBytes([]byte(string(parent) + " " + string(dgsts[0])))
    return createChainIDFromParent(ChainID(dgst), dgsts[1:]...)
}
```

* layer.cacheID

此外，还有一个`layer.cacheID`用于生成`graph driver`的目录：

```go
///layer/ro_layer.go
type roLayer struct {
    chainID    ChainID
    diffID     DiffID
    parent     *roLayer
    cacheID    string  ///用于graph driver
    size       int64
    layerStore *layerStore
    descriptor distribution.Descriptor

    referenceCount int
    references     map[Layer]struct{}
}
```

```go
///layer/layer_store.go
func (ls *layerStore) Register(ts io.Reader, parent ChainID) (Layer, error) {
///...
  // Create new roLayer
  layer := &roLayer{
    parent:         p,
    cacheID:        stringid.GenerateRandomID(),
    referenceCount: 1,
    layerStore:     ls,
    references:     map[Layer]struct{}{},
    descriptor:     descriptor,
  }

  if err = ls.driver.Create(layer.cacheID, pid, "", nil); err != nil { ///创建graph driver目录
    return nil, err
  }
///...
}
```

### 5.2 Stored format

在本地，有2个与镜像存储相关的目录，以overlayfs为例，`ROOT/image/overlay`和`ROOT/overlay`：

前者保存image的信息，包括`imagedb`和`layerdb`：

```sh
# ls image/overlay/
distribution  imagedb  layerdb  repositories.json
```

后者用于存储实际的数据。更多参考[这里](https://gist.github.com/aaronlehmann/b42a2eaf633fc949f93b#interfaces)。

* imagedb

保存image的信息。

```
### image ID为目录名称
# ls image/overlay/imagedb/content/sha256/
9e301a362a270bcb6900ebd1aad1b3a9553a9d055830bdf4cab5c2184187a2d1
```

* layerdb

保存layer的信息。

```sh
### image/overlay/layerdb/sha256/保存每个layer的元数据信息，目录名称为layer的chainID
# ls image/overlay/layerdb/sha256/ -R                                                            
image/overlay/layerdb/sha256/:
75a46a4a46d9b53d8bbd70d52a26dc08858961f51156372edf6e8084ba9cfdb6  ae2b342b32f9ee27f0196ba59e9952c00e016836a11921ebc8baaf783847686a

image/overlay/layerdb/sha256/75a46a4a46d9b53d8bbd70d52a26dc08858961f51156372edf6e8084ba9cfdb6:
cache-id  diff  parent  size  tar-split.json.gz

image/overlay/layerdb/sha256/ae2b342b32f9ee27f0196ba59e9952c00e016836a11921ebc8baaf783847686a:
cache-id  diff  size  tar-split.json.gz
```

cache-id:

```sh
#### cache-id 为graph driver的目录名称
# cat image/overlay/layerdb/sha256/ae2b342b32f9ee27f0196ba59e9952c00e016836a11921ebc8baaf783847686a/cache-id   
5b0bd80f386aebead9ee65c7d18172e6d937303bcc63490ef1378f92c1fb9a36

# cat image/overlay/layerdb/sha256/75a46a4a46d9b53d8bbd70d52a26dc08858961f51156372edf6e8084ba9cfdb6/cache-id 
390c9a37d1edca218961b4133c67992b6e62b1b1c1cdf820494ff4df5b9fce7c

# ls overlay/
390c9a37d1edca218961b4133c67992b6e62b1b1c1cdf820494ff4df5b9fce7c  5b0bd80f386aebead9ee65c7d18172e6d937303bcc63490ef1378f92c1fb9a36
```

diff:

```sh
### diff保存当前layer的diff ID
# cat image/overlay/layerdb/sha256/ae2b342b32f9ee27f0196ba59e9952c00e016836a11921ebc8baaf783847686a/diff     
sha256:ae2b342b32f9ee27f0196ba59e9952c00e016836a11921ebc8baaf783847686a ###chainID == diffID(bottom layer)

# cat image/overlay/layerdb/sha256/75a46a4a46d9b53d8bbd70d52a26dc08858961f51156372edf6e8084ba9cfdb6/diff 
sha256:5f70bf18a086007016e948b04aed3b82103a36bea41755b6cddfaf10ace3c6ef
```

parent:

```sh
### parent保存上一层layer的chainID
# cat image/overlay/layerdb/sha256/75a46a4a46d9b53d8bbd70d52a26dc08858961f51156372edf6e8084ba9cfdb6/parent 
sha256:ae2b342b32f9ee27f0196ba59e9952c00e016836a11921ebc8baaf783847686a
```

* distribution

另外，还有一个`distribution`目录：

```sh
# ls /data/docker/image/overlay/distribution/
diffid-by-digest  v2metadata-by-diffid
```

对应

```go
// FSMetadataStore uses the filesystem to associate metadata with layer and
// image IDs.
type FSMetadataStore struct {
  sync.RWMutex
  basePath string ///filepath.Join(imageRoot, "distribution")
}
```

其中`diffid-by-digest`保存了digest->diffid的映射关系，即`distribution hashes`和`Content hashes`的映射关系。这可以方便检查layer是否在本地已经存在。

```sh
# cat /data/docker/image/overlay/distribution/diffid-by-digest/sha256/c0a04912aa5afc0b4fd4c34390e526d547e67431f6bc122084f1e692dcb7d34e 
sha256:ae2b342b32f9ee27f0196ba59e9952c00e016836a11921ebc8baaf783847686a

# cat /data/docker/image/overlay/distribution/diffid-by-digest/sha256/a3ed95caeb02ffe68cdd9fd84406680ae93d633cb16422d00e8a7c22955b46d4 
sha256:5f70bf18a086007016e948b04aed3b82103a36bea41755b6cddfaf10ace3c6ef
```

`v2metadata-by-diffid`保存了diffid -> (digest,repository)的映射关系，这可以方便查找layer的digest及其所属的repository:

```sh
# cat /data/docker/image/overlay/distribution/v2metadata-by-diffid/sha256/ae2b342b32f9ee27f0196ba59e9952c00e016836a11921ebc8baaf783847686a   
[{"Digest":"sha256:c0a04912aa5afc0b4fd4c34390e526d547e67431f6bc122084f1e692dcb7d34e","SourceRepository":"10.x.x.x:5000/busybox"}]
```

几个问题：

(1)如何判断image在本地已经存在？

实际上，schema2的返回结果已经包含image ID，只需要从image store检查即可：

```go
func (p *v2Puller) pullSchema2(ctx context.Context, ref reference.Named, mfst *schema2.DeserializedManifest) (imageID image.ID, manifestDigest digest.Digest, err error) {
  manifestDigest, err = schema2ManifestDigest(ref, mfst)
  if err != nil {
    return "", "", err
  }

  target := mfst.Target()
  imageID = image.ID(target.Digest)
  if _, err := p.config.ImageStore.Get(imageID); err == nil { ///image ID已经存在
    // If the image already exists locally, no need to pull
    // anything.
    return imageID, manifestDigest, nil
  }
```

(2)如何判断layer在本地已经存在？

```go
func (ldm *LayerDownloadManager) Download(ctx context.Context, initialRootFS image.RootFS, layers []DownloadDescriptor, progressOutput progress.Output) (image.RootFS, func(), error) {
  rootFS := initialRootFS
  for _, descriptor := range layers {
    key := descriptor.Key()
    transferKey += key

    if !missingLayer {
      missingLayer = true
      diffID, err := descriptor.DiffID()
      if err == nil {
        getRootFS := rootFS
        getRootFS.Append(diffID)
        l, err := ldm.layerStore.Get(getRootFS.ChainID())
        if err == nil { ////layer已经存在
          // Layer already exists.
          logrus.Debugf("Layer already exists: %s", descriptor.ID())
          progress.Update(progressOutput, descriptor.ID(), "Already exists")
          if topLayer != nil {
            layer.ReleaseAndLog(ldm.layerStore, topLayer)
          }
          topLayer = l
          missingLayer = false
          rootFS.Append(diffID)
          continue
        }
      }
    }
///...
}


func (ld *v2LayerDescriptor) DiffID() (layer.DiffID, error) {
  return ld.V2MetadataService.GetDiffID(ld.digest)
}

///ditribution/metadata/v2_metadata_service.go
// GetDiffID finds a layer DiffID from a digest.
func (serv *V2MetadataService) GetDiffID(dgst digest.Digest) (layer.DiffID, error) {
  ///distribution/diffid-by-digest/sha256/$digest
  diffIDBytes, err := serv.store.Get(serv.digestNamespace(), serv.digestKey(dgst))
  if err != nil {
    return layer.DiffID(""), err
  }

  return layer.DiffID(diffIDBytes), nil
}

func (serv *V2MetadataService) diffIDNamespace() string {
  return "v2metadata-by-diffid"
}

func (serv *V2MetadataService) digestNamespace() string {
  return "diffid-by-digest"
}

func (serv *V2MetadataService) diffIDKey(diffID layer.DiffID) string {
  return string(digest.Digest(diffID).Algorithm()) + "/" + digest.Digest(diffID).Hex()
}

func (serv *V2MetadataService) digestKey(dgst digest.Digest) string {
  return string(dgst.Algorithm()) + "/" + dgst.Hex()
}



///ditribution/metadata/metadata.go
// Get retrieves data by namespace and key. The data is read from a file named
// after the key, stored in the namespace's directory.
func (store *FSMetadataStore) Get(namespace string, key string) ([]byte, error) {
  store.RLock()
  defer store.RUnlock()

  return ioutil.ReadFile(store.path(namespace, key))
}
```

## 6 Summary

新的存储格式涉及的ID比较多，理解了这些ID，也就理解了存储格式。这里简单总结一下：

(1)Image ID与Layer ID的计算方法不同。Image ID是在本地由Docker根据镜像的描述文件计算的，并用于imagedb的目录名称。Layer ID一般指`Distribution`根据layer compressed data计算的，并用于`Distribution`的存储目录名称。

(2)`layer.DiffID`是在本地由Docker根据layer uncompressed data计算的。`layer.ChainID`只用本地，根据`layer.DiffID`计算，并用于layerdb的目录名称。

(3)layer.cacheID随机生成，用于graph driver的目录名称。

## 7 Related

* [Docker Image Specification v1.2.0](https://github.com/docker/docker/blob/master/image/spec/v1.2.md)
* [Docker Registry HTTP API V2](https://github.com/docker/distribution/blob/master/docs/spec/api.md)
* [1.10 Distribution Changes Design Doc](https://gist.github.com/aaronlehmann/b42a2eaf633fc949f93b)
* [Image Manifest Version 2, Schema 1](https://github.com/docker/distribution/blob/master/docs/spec/manifest-v2-1.md)
* [Image Manifest Version 2, Schema 2](https://github.com/docker/distribution/blob/master/docs/spec/manifest-v2-2.md)

