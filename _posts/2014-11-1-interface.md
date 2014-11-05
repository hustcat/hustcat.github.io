---
date: 2014-11-01 22:47:30+00:00
layout: post
title:  "Go语言学习：接口"
categories: 编程语言
tags: go
excerpt: 如果说goroutine和channel是Go并发的两大基石，那么接口是Go语言编程中数据类型的关键。在Go语言的实际编程中，几乎所有的数据结构都围绕接口展开，接口是Go语言中所有数据结构的核心
---

概述
------
如果说goroutine和channel是Go并发的两大基石，那么接口是Go语言编程中数据类型的关键。在Go语言的实际编程中，几乎所有的数据结构都围绕接口展开，接口是Go语言中所有数据结构的核心。

Go语言中的接口是一些方法的集合（method set），它指定了对象的行为：如果它（任何数据类型）可以做这些事情，那么它就可以在这里使用。

```go
type Reader interface {
	Read(p []byte) (n int, err error)
}

type Writer interface {
	Write(p []byte) (n int, err error)
}

type Closer interface {
	Close() error
}

type Seeker interface {
	Seek(offset int64, whence int) (int64, error)
}
```
上面的代码定义了4个接口。

假设我们在另一个地方中定义了下面这个结构体：

```go
type File struct { // ...
}
func (f *File) Read(buf []byte) (n int, err error)
func (f *File) Write(buf []byte) (n int, err error)
func (f *File) Seek(off int64, whence int) (pos int64, err error) func (f *File) Close() error
```
我们在实现File的时候，可能并不知道上面4个接口的存在，但不管怎样，File实现了上面所有的4个接口。我们可以将File对象赋值给上面任何一个接口。

```go
var file1 Reader = new(File) 
var file2 Writer = new(File) 
var file3 Closer = new(File)
var file4 Seeker = new(File)
```
因为File可以做这些事情，所以，File就可以在这里使用。File在实现的时候，并不需要指定实现了哪个接口，它甚至根本不知道这4个接口的存在。这种“松耦合”的做法完全不同于传统的面向对象编程。

实际上，上面4个接口来自标准库中的io package。

接口赋值
------
我们可以将一个实现接口的对象实例赋值给接口，也可以将另外一个接口赋值给接口。

**（1）通过对象实例赋值**

将一个对象实例赋值给一个接口之前，要保证该对象实现了接口的所有方法。考虑如下示例：

```go
type Integer int
func (a Integer) Less(b Integer) bool {
	return a < b
}
func (a *Integer) Add(b Integer) {
	*a += b
}

type LessAdder interface { 
	Less(b Integer) bool 
	Add(b Integer)
}

var a Integer = 1
var b1 LessAdder = &a //OK
var b2 LessAdder = a   //not OK
```
b2的赋值会报编译错误，为什么呢？还记得<类型方法>一章中讨论的Go语言规范的规定吗？

> The method set of any other named type T consists of all methods with receiver type T. The method set of the corresponding pointer type *T is the set of all methods with receiver *T or T (that is, it also contains the method set of T).

也就是说*Integer实现了接口LessAdder的所有方法，而Integer只实现了Less方法，所以不能赋值。

**（2）通过接口赋值**

```go
        var r io.Reader = new(os.File)
        var rw io.ReadWriter = r   //not ok

        var rw2 io.ReadWriter = new(os.File)
        var r2 io.Reader = rw2    //ok
```
因为r没有Write方法，所以不能赋值给rw。

接口嵌套
------
我们来看看io package中的另外一个接口：

```go
// ReadWriter is the interface that groups the basic Read and Write methods.
type ReadWriter interface {
	Reader
	Writer
}
```
该接口嵌套了io.Reader和io.Writer两个接口，实际上，它等同于下面的写法：

```go
type ReadWriter interface {
Read(p []byte) (n int, err error) 
Write(p []byte) (n int, err error)
}
```
注意，Go语言中的接口不能递归嵌套，

```go
// illegal: Bad cannot embed itself
type Bad interface {
	Bad
}

// illegal: Bad1 cannot embed itself using Bad2
type Bad1 interface {
	Bad2
}
type Bad2 interface {
	Bad1
}
```

空接口（empty interface）
------
空接口比较特殊，它不包含任何方法：

```go
interface{}
```
在Go语言中，所有其它数据类型都实现了空接口。

```go
var v1 interface{} = 1
var v2 interface{} = "abc"
var v3 interface{} = struct{ X int }{1}
```
如果函数打算接收任何数据类型，则可以将参考声明为interface{}。最典型的例子就是标准库fmt包中的Print和Fprint系列的函数：

```go
func Fprint(w io.Writer, a ...interface{}) (n int, err error) 
func Fprintf(w io.Writer, format string, a ...interface{})
func Fprintln(w io.Writer, a ...interface{})
func Print(a ...interface{}) (n int, err error)
func Printf(format string, a ...interface{})
func Println(a ...interface{}) (n int, err error)
```
注意，[]T不能直接赋值给[]interface{}

```go
        t := []int{1, 2, 3, 4}
        var s []interface{} = t
```
编译时会输出下面的错误：

> cannot use t (type []int) as type []interface {} in assignment

我们必须通过下面这种方式：

```go
t := []int{1, 2, 3, 4}
s := make([]interface{}, len(t))
for i, v := range t {
    s[i] = v
}
```

类型转换（Conversions）
------
类型转换的语法：

```go
Conversion = Type "(" Expression [ "," ] ")" .
```
当以运算符*或者<-开始，有必要加上括号避免模糊：

```go
*Point(p)        // same as *(Point(p))
(*Point)(p)      // p is converted to *Point
<-chan int(c)    // same as <-(chan int(c))
(<-chan int)(c)  // c is converted to <-chan int
func()(x)        // function signature func() x
(func())(x)      // x is converted to func()
(func() int)(x)  // x is converted to func() int
func() int(x)    // x is converted to func() int (unambiguous)
```

Type switch与Type assertions
------
在Go语言中，我们可以使用type switch语句查询接口变量的真实数据类型，语法如下：

```go
switch x.(type) {
// cases
}
```
x必须是接口类型。

来看一个详细的示例：

```go
type Stringer interface {
    String() string
}

var value interface{} // Value provided by caller.
switch str := value.(type) {
case string:
    return str //type of str is string
case Stringer: //type of str is Stringer
    return str.String()
}
```
语句switch中的value必须是接口类型，变量str的类型为转换后的类型。

> If the switch declares a variable in the expression, the variable will have the corresponding type in each clause. It's also idiomatic to reuse the name in such cases, in effect declaring a new variable with the same name but a different type in each case.

如果我们只关心一种类型该如何做？如果我们知道值为一个string，只是想将它抽取出来该如何做？只有一个case的类型switch是可以的，不过也可以用类型断言（type assertions）。类型断言接受一个接口值，从中抽取出显式指定类型的值。其语法借鉴了类型switch子句，不过是使用了显式的类型，而不是type关键字，如下：

```go
x.(T)
```
同样，x必须是接口类型。

```go
str := value.(string)
```
上面的转换有一个问题，如果该值不包含一个字符串，则程序会产生一个运行时错误。为了避免这个问题，可以使用“comma, ok”的习惯用法来安全地测试值是否为一个字符串：

```go
str, ok := value.(string)
if ok {
    fmt.Printf("string value is: %q\n", str)
} else {
    fmt.Printf("value is not a string\n")
}
```
如果类型断言失败，则str将依然存在，并且类型为字符串，不过其为零值，即一个空字符串。
我们可以使用类型断言来实现type switch的中例子：

```go
if str, ok := value.(string); ok {
    return str
} else if str, ok := value.(Stringer); ok {
    return str.String()
}
```
这种做法没有多大实用价值。
