---
layout: post
title:  Docker解析：Docker拉取镜像(docker pull)的实现
date: 2014-12-19 00:21:30
categories: Linux
tags: docker
excerpt: Docker拉取镜像(docker pull)的实现
---

基本流程
------

docker pull的基本流程如下
![](/assets/2014-12-17-docker-pull-1.png)

（1）docker首先查询index，看哪里可以下载samalba/busybox；

（2）index返回samalba/busybox的image List、registry URL和token:

```go
func (r *Session) GetRepositoryData(remote string) (*RepositoryData, error) {
...
	return &RepositoryData{
		ImgList:   imgsData,
		Endpoints: endpoints,
		Tokens:    tokens,
	}, nil
}
```

（3）docker向registry请求数据；

（4）registry向index验证token/user是否可以下载该image；

（5）index返回验证结果true/false ；

（6）registry给docker返回image数据。

主要参考
https://docs.docker.com/reference/api/hub_registry_spec/

内部实现
------

主要函数调用：
![](/assets/2014-12-17-docker-pull.jpg)

```go
graph/pull.go
(s *TagStore) pullRepository(r *registry.Session, out io.Writer, localName, remoteName, askedTag string, sf *utils.StreamFormatter, parallel bool, mirrors []string)
{
...
//v1/repositories/dbyin/tlinux1.2/images
repoData, err := r.GetRepositoryData(remoteName)

//v1/repositories/dbyin/tlinux1.2/tags
tagsList, err := r.GetRemoteTags(repoData.Endpoints, remoteName, repoData.Tokens)
///依次拉取镜像
for _, image := range repoData.ImgList {
        //v1/images/"+imgID+"/ancestry
	history, err := r.GetRemoteHistory(imgID, endpoint, token)
	for i := len(history) - 1; i >= 0; i-- {
		id := history[i]

		//v1/images/"+imgID+"/json
		imgJSON, imgSize, err = r.GetRemoteImageJSON(id, endpoint, token)
               
		//v1/images/"+imagID+"/layer
               layer, err := r.GetRemoteImageLayer(img.ID, endpoint, token, int64(imgSize))
		
		s.graph.Register(img,
					utils.ProgressReader(layer, imgSize, out, sf, false, utils.TruncateID(id), "Downloading"))
	}       
}

}
```

整个流程大致如下：

（1）通过名称（例如dbyin/tlinux1.2）获取镜像列表imagelist，调用接口为

> v1/repositories/dbyin/tlinux1.2/images

> registry实际上读取文件repositories/dbyin/tlinux1.2/_index_images

（2）获取tag 列表，调用接口为

> v1/repositories/dbyin/tlinux1.2/tags

（3）依次拉取所有镜像：

3.1）获取镜像的祖先镜像列表ancestry，调用接口为

> v1/images/"+imgID+"/ancestry

> registry实际上读取images/${image-id}/ancestry；

3.2）从最早的祖先镜像开始，依次拉取每一层镜像，调用接口为

> //获取镜像描述文件

> v1/images/"+imgID+"/json 

> //获取该层的数据

> v1/images/"+imagID+"/layer

函数Register完成将本层的数据写到本地的graph：

```go
//image/image.go
//img：镜像描述信息
//layerData: 本层数据
(graph *Graph) Register(img *image.Image, layerData archive.ArchiveReader){

//创建临时目录/var/lib/docker/graph/_tmp/${random_id}
tmp, err := graph.Mktemp("")

//创建thin volume
err := graph.driver.Create(img.ID, img.Parent)

//layerData写到img.ID的thin volume，元数据信息(json,layersize)写到tmp
image.StoreImage(img, layerData, tmp)

//目录/var/lib/docker/graph/_tmp/${random_id}->/var/lib/docker/graph/${image-id}
err := os.Rename(tmp, graph.ImageRoot(img.ID));

graph.idIndex.Add(img.ID)
```

并行pull
------

```go
// Creates an image from Pull or from Import
func postImagesCreate(eng *engine.Engine, version version.Version, w http.ResponseWriter, r *http.Request, vars map[string]string) error {
...
	if image != "" { //pull
		if tag == "" {
			image, tag = parsers.ParseRepositoryTag(image)
		}
		metaHeaders := map[string][]string{}
		for k, v := range r.Header {
			if strings.HasPrefix(k, "X-Meta-") {
				metaHeaders[k] = v
			}
		}
		job = eng.Job("pull", image, tag)
		job.SetenvBool("parallel", version.GreaterThan("1.3"))
```

并没有依赖的镜像之间是可以并行拉取的，1.3开始支持并行pull。

参考
https://groups.google.com/forum/#!topic/docker-user/MlepnxkPjn8
