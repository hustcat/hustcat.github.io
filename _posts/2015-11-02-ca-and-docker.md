---
layout: post
title: CA and docker
date: 2015-11-02 18:16:30
categories: Linux
tags: docker CA
excerpt: CA and docker
---

最近在自建的docker-registry上遇到证书过期的问题，给运营带来了很大的麻烦。主要是因为docker(1.3.x)不能动态加载CA证书，导致必须重启docker daemon才能解决。不得不说，自己给自己挖了一个很大的坑。简单总结一下，以警后人。

# 错误

在自建的docker-registry时，经常会遇到下面的问题：

* （1）找不到对应的CA

```
Error response from daemon: Invalid registry endpoint https://hub.oa.com/v1/: Get https://hub.oa.com/v1/_ping: x509: certificate signed by unknown authority. If this private registry supports only HTTP or HTTPS with an unknown CA certificate, please add `--insecure-registry hub.oa.com` to the daemon's arguments. In the case of HTTPS, if you have access to the registry's CA certificate, no need for the flag; simply place the CA certificate at /etc/docker/certs.d/hub.oa.com/ca.crt
```

* （2）证书过期

```
Invalid registry endpoint https://hub.oa.com/v1/: Get https://hub.oa.com/v1/_ping: x509: certificate has expired or is not yet valid. If this private registry supports only HTTP or HTTPS with an unknown CA certificate, please add `--insecure-registry hub.oa.com` to the daemon's arguments. In the case of HTTPS, if you have access to the registry's CA certificate, no need for the flag; simply place the CA certificate at /etc/docker/certs.d/hub.oa.com/ca.crt
```

# Docker为什么不能动态加载CA?

这里要从两方面讨论，一是go语言本身，另外就是Docker。

*** golang ***

go标准库本身已经实现TLS 1.2协议。来看看go加载CA的逻辑：

```go
crypto/x509/verify.go
// UnknownAuthorityError results when the certificate issuer is unknown
type UnknownAuthorityError struct {
    cert *Certificate
    // hintErr contains an error that may be helpful in determining why an
    // authority wasn't found.
    hintErr error
    // hintCert contains a possible authority certificate that was rejected
    // because of the error in hintErr.
    hintCert *Certificate
}

func (e UnknownAuthorityError) Error() string {
    s := "x509: certificate signed by unknown authority"
    if e.hintErr != nil {
        certName := e.hintCert.Subject.CommonName
        if len(certName) == 0 {
            if len(e.hintCert.Subject.Organization) > 0 {
                certName = e.hintCert.Subject.Organization[0]
            }
            certName = "serial:" + e.hintCert.SerialNumber.String()
        }
        s += fmt.Sprintf(" (possibly because of %q while trying to verify candidate authority certificate %q)", e.hintErr, certName)
    }
    return s
}
```

golang加载CA

```go
crypto/x509/root_unix.go
// Possible certificate files; stop after finding one.
var certFiles = []string{
    "/etc/ssl/certs/ca-certificates.crt",     // Debian/Ubuntu/Gentoo etc.
    "/etc/pki/tls/certs/ca-bundle.crt",       // Fedora/RHEL
    "/etc/ssl/ca-bundle.pem",                 // OpenSUSE
    "/etc/ssl/cert.pem",                      // OpenBSD
    "/usr/local/share/certs/ca-root-nss.crt", // FreeBSD/DragonFly
}

func (c *Certificate) systemVerify(opts *VerifyOptions) (chains [][]*Certificate, err error) {
    return nil, nil
}

func initSystemRoots() {
    roots := NewCertPool()
    for _, file := range certFiles {
        data, err := ioutil.ReadFile(file)
        if err == nil {
            roots.AppendCertsFromPEM(data)
            systemRoots = roots
            return
        }
    }

    // All of the files failed to load. systemRoots will be nil which will
    // trigger a specific error at verification time.
}
```

但这个逻辑只会运行一次：

```go
crypto/x509/root.go
func systemRootsPool() *CertPool {
    once.Do(initSystemRoots)
    return systemRoots
}
```

也就是说对于go程序，进程只会运行一次。

*** docker ***

再看看Docker加载CA的逻辑：

```go
func NewEndpoint(hostname string, insecureRegistries []string) (*Endpoint, error) {
    endpoint, err := newEndpoint(hostname, insecureRegistries)
    if err != nil {
        return nil, err
    }

    // Try HTTPS ping to registry
    endpoint.URL.Scheme = "https"
    if _, err := endpoint.Ping(); err != nil {
…


func (e Endpoint) Ping() (RegistryInfo, error) {
    if e.String() == IndexServerAddress() {
        // Skip the check, we now this one is valid
        // (and we never want to fallback to http in case of error)
        return RegistryInfo{Standalone: false}, nil
    }

    req, err := http.NewRequest("GET", e.String()+"_ping", nil)
    if err != nil {
        return RegistryInfo{Standalone: false}, err
    }

    resp, _, err := doRequest(req, nil, ConnectTimeout, e.secure)
    if err != nil {
        return RegistryInfo{Standalone: false}, err
    }
```

但实际上，doRequest是有读取CA的逻辑的，但由于前面访问https://{host}/v1/_ping没有读取CA，所以，这里没有什么用。

```go
func doRequest(req *http.Request, jar http.CookieJar, timeout TimeoutType, secure bool) (*http.Response, *http.Client, error) {
    var (
        pool  *x509.CertPool
        certs []*tls.Certificate
    )

    if secure && req.URL.Scheme == "https" {
        hasFile := func(files []os.FileInfo, name string) bool {
            for _, f := range files {
                if f.Name() == name {
                    return true
                }
            }
            return false
        }

        hostDir := path.Join("/etc/docker/certs.d", req.URL.Host)
        log.Debugf("hostDir: %s", hostDir)
        fs, err := ioutil.ReadDir(hostDir)
        if err != nil && !os.IsNotExist(err) {
            return nil, nil, err
        }

        for _, f := range fs {
            if strings.HasSuffix(f.Name(), ".crt") {
                if pool == nil {
                    pool = x509.NewCertPool()
                }
                log.Debugf("crt: %s", hostDir+"/"+f.Name())
                data, err := ioutil.ReadFile(path.Join(hostDir, f.Name()))
                if err != nil {
                    return nil, nil, err
                }
                pool.AppendCertsFromPEM(data)
            }
            if strings.HasSuffix(f.Name(), ".cert") {
                certName := f.Name()
                keyName := certName[:len(certName)-5] + ".key"
                log.Debugf("cert: %s", hostDir+"/"+f.Name())
                if !hasFile(fs, keyName) {
                    return nil, nil, fmt.Errorf("Missing key %s for certificate %s", keyName, certName)
                }
                cert, err := tls.LoadX509KeyPair(path.Join(hostDir, certName), path.Join(hostDir, keyName))
                if err != nil {
                    return nil, nil, err
                }
                certs = append(certs, &cert)
            }
            if strings.HasSuffix(f.Name(), ".key") {
                keyName := f.Name()
                certName := keyName[:len(keyName)-4] + ".cert"
                log.Debugf("key: %s", hostDir+"/"+f.Name())
                if !hasFile(fs, certName) {
                    return nil, nil, fmt.Errorf("Missing certificate %s for key %s", certName, keyName)
                }
            }
        }
    }

    if len(certs) == 0 {
        client := newClient(jar, pool, nil, timeout, secure)
        res, err := client.Do(req)
        if err != nil {
            return nil, nil, err
        }
        return res, client, nil
    }

    for i, cert := range certs {
        client := newClient(jar, pool, cert, timeout, secure)
        res, err := client.Do(req)
        // If this is the last cert, otherwise, continue to next cert if 403 or 5xx
        if i == len(certs)-1 || err == nil && res.StatusCode != 403 && res.StatusCode < 500 {
            return res, client, err
        }
    }

    return nil, nil, nil
}
```

# docker + nginx

## 创建自己的CA

一般来说，我们应该使用权威CA（Certification Authority）机构签名的证书（Certificates）。为了简单，我们这里使用自己签名（Self-Signed）的证书。

在给nginx创建签名的证书之前，我们先要创建一个我们自己的CA，CA包含公钥和私钥，私钥用于给其它证书签名，公钥用于别人验证证书的有效性

```sh
# echo 01 > ca.srl
# openssl genrsa -des3 -out ca-key.pem 2048
Generating RSA private key, 2048 bit long modulus
..............................+++
..............................................................................................................................................................+++
e is 65537 (0x10001)
Enter pass phrase for ca-key.pem:
Verifying - Enter pass phrase for ca-key.pem:

# openssl req -new -x509 -days 7300 -key ca-key.pem -out ca.pem
Enter pass phrase for ca-key.pem:
You are about to be asked to enter information that will be incorporated
into your certificate request.
What you are about to enter is what is called a Distinguished Name or a DN.
There are quite a few fields but you can leave some blank
For some fields there will be a default value,
If you enter '.', the field will be left blank.
-----
Country Name (2 letter code) [XX]:CN
State or Province Name (full name) []:Guangdong
Locality Name (eg, city) [Default City]:Shenzhen
Organization Name (eg, company) [Default Company Ltd]:Big Company
Organizational Unit Name (eg, section) []: IEG
Common Name (eg, your name or your server's hostname) []:dev.registry.com
Email Address []:hustcat@gmail.com
```

现在，我们有了一个自己的CA，就可以为nginx创建证书了。值得注意的是，参考'-days'指定CA的有效期，一般来说，root CA建议为20年。root CA一旦过期，其签发的所有子证书也相应过期。

*** 查看private key内容： ***

```sh
# cat ca-key.pem 
-----BEGIN RSA PRIVATE KEY-----
Proc-Type: 4,ENCRYPTED
DEK-Info: DES-EDE3-CBC,47AB85812305F38C

P3u3362GHPv0wFY23WzrI7q+8DvaoSFUHtOjy8WfHRPCufl4tdbjpwNUTs/MtkvH
m8KK5vR2vIfn2Y7Gt07AHeITO+lgN+POltt8N64bVRyQH8tStfk1xMuLr/ijpqE3
...
-----END RSA PRIVATE KEY-----
```

或者：

```sh
# openssl rsa -text -in ca-key.pem 
Enter pass phrase for ca-key.pem:
Private-Key: (2048 bit)
modulus:
    00:e6:b4:f4:ce:5b:95:a3:28:e0:9b:ea:63:1f:bd:
...
publicExponent: 65537 (0x10001)
privateExponent:
    00:89:04:d2:1c:39:22:8f:2b:b8:f2:80:eb:6c:2f:
...
prime1:
    00:f6:56:4a:67:14:cc:00:b2:17:a6:f9:29:60:ce:
...
prime2:
    00:ef:c1:b5:20:3c:57:65:2d:7a:76:ee:df:4e:1d:
...
exponent1:
    6c:e7:ed:70:f1:42:d7:b3:e4:bd:0f:3e:f9:eb:6d:
...
exponent2:
    00:c2:04:35:38:92:db:fe:4a:61:31:e1:77:df:54:
...
coefficient:
    00:f0:9b:be:ba:3e:e5:da:19:57:bf:a8:e0:ff:ae:
...
writing RSA key
-----BEGIN RSA PRIVATE KEY-----
MIIEpQIBAAKCAQEA5rT0zluVoyjgm+pjH73AGb+ZkZ3B3jDzJ/hQiLGKFFaIYhUW
Ahcnp7nLAk0zG3+s6LnTrjRkqiAjYPZLDNy9YeERbXfOZNdmPPQijE6HXvvVmBwY
...
-----END RSA PRIVATE KEY-----
```

*** 查看CA 内容：***

```sh
# openssl x509 -in ca.pem -text -noout
Certificate:
    Data:
        Version: 3 (0x2)
        Serial Number: 11706049573231839044 (0xa2743e10c798c744)
    Signature Algorithm: sha1WithRSAEncryption
        Issuer: C=CN, ST=Guangdong, L=Shenzhen, O=Big Company, OU=IEG, CN=dev.registry.com/emailAddress=hustcat@gmail.com
        Validity
            Not Before: Oct 29 13:30:42 2015 GMT
            Not After : Oct 28 13:30:42 2016 GMT
        Subject: C=CN, ST=Guangdong, L=Shenzhen, O=Big Company, OU=IEG, CN=dev.registry.com/emailAddress=hustcat@gmail.com
        Subject Public Key Info:
            Public Key Algorithm: rsaEncryption
                Public-Key: (2048 bit)
                Modulus:
                    00:e6:b4:f4:ce:5b:95:a3:28:e0:9b:ea:63:1f:bd:
...
                Exponent: 65537 (0x10001)
        X509v3 extensions:
            X509v3 Subject Key Identifier: 
                2F:29:0A:27:A1:E3:76:92:B5:D9:2D:D8:E4:B5:72:FB:1E:BA:5A:0F
            X509v3 Authority Key Identifier: 
                keyid:2F:29:0A:27:A1:E3:76:92:B5:D9:2D:D8:E4:B5:72:FB:1E:BA:5A:0F

            X509v3 Basic Constraints: 
                CA:TRUE
    Signature Algorithm: sha1WithRSAEncryption
         16:6c:02:e5:44:99:56:50:b5:19:46:a7:7f:18:d6:32:ce:d7:
         bc:fa:ac:c1:e1:87:6c:17:22:f4:1a:52:80:38:50:8d:49:be:
...
```

## 为nginx创建子证书

```sh
# openssl genrsa -des3 -out server-key.pem 2048
Generating RSA private key, 2048 bit long modulus
.....+++
.................+++
e is 65537 (0x10001)
Enter pass phrase for server-key.pem:
Verifying - Enter pass phrase for server-key.pem:

# openssl req -subj '/CN=dev.registry.com' -new -key server-key.pem -out server.csr
Enter pass phrase for server-key.pem:

# openssl x509 -req -days 365 -in server.csr -CA ca.pem -CAkey ca-key.pem -out server-cert.pem
Signature ok
subject=/CN=dev.registry.com
Getting CA Private Key
Enter pass phrase for ca-key.pem:
```

我们删除server key中的passphrase，

```sh
# openssl rsa -in server-key.pem -out server-key.pem
Enter pass phrase for server-key.pem:
writing RSA key
```

然后我们安装server-key和server-crt。

```sh
# cp server-cert.pem /etc/ssl/certs/docker-registry.crt
# cp server-key.pem /etc/ssl/private/docker-registry.key
```

到这里，针对nginx的SSL证书就算配置好了。启动nginx即可。


# 相关资料
* [OpenSSL Cookbook](https://www.feistyduck.com/library/openssl-cookbook/online/ch-openssl.html)
* [Certification authority root certificate expiry and renewal](http://serverfault.com/questions/306345/certification-authority-root-certificate-expiry-and-renewal)
* [Trusting additional CAs in Fedora / RHEL / CentOS: an alternative to editing /etc/pki/tls/certs/ca-bundle.crt (or /etc/pki/tls/cert.pem)](https://www.happyassassin.net/2015/01/14/trusting-additional-cas-in-fedora-rhel-centos-dont-append-to-etcpkitlscertsca-bundle-crt-or-etcpkitlscert-pem/)
* [Go和HTPPS](http://tonybai.com/2015/04/30/go-and-https/)

