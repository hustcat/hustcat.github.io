---
date: 2014-11-01 22:40:30+00:00
layout: post
title: Go语言学习：new与make
categories: 编程语言
tags: go
excerpt: Go语言中的内建函数new和make是两个用于内存分配的原语（allocation primitives）。对于初学者，这两者的区别也挺容易让人迷糊的。简单的说，new只分配内存，make用于slice，map，和channel的初始化。
---

Go语言中的内建函数new和make是两个用于内存分配的原语（allocation primitives）。对于初学者，这两者的区别也挺容易让人迷糊的。简单的说，new只分配内存，make用于slice，map，和channel的初始化。

new
------
这是一个用来分配内存的内建函数，但是与C++不一样的是，它并不初始化内存，只是将其置零。也就是说，new(T)会为T类型的新项目，分配被置零的存储，并且返回它的地址，一个类型为*T的值。在Go的术语中，其返回一个指向新分配的类型为T的指针，这个指针指向的内容的值为零（zero value）。注意并不是指针为零。

Go语言中的对象没有C++中的构造函数，如果用C来描述，Go中的new大概相当于：

```go
T *t = (T*)malloc(sizeof(T))
memset(t, 0, sizeof(T))
```
其实，上面的描可能也不是很准确，也许用*t=zerovalue更准确。因为对于不同的数据类型，零值的意义是完全不一样的。比如，对于bool类型，零值为false；int的零值为0；string的零值是空字符串：

```go
        b := new(bool)
        fmt.Println(*b)
        i := new(int)
        fmt.Println(*i)
        s := new(string)
        fmt.Println(*s)
```
输出：

```go
false
0

```
注意最后有一个空字符串。

初始化
------
很多时候，零值并不是一个好主意，我们需要做一些初始化。考虑如下结构体：

```go
type Rect struct { 
x, y float64
width, height float64 
}
```
零值的Rect并没有多大用处，我们以下方式进行初始化：

```go
rect3 := &Rect{0, 0, 100, 200}
rect4 := &Rect{width: 100, height: 200}
```
再申明一下，Go语言中没有C++中的构造函数，对象的创建一般交给一个全局的创建函数来完成：

```go
func NewRect(x, y, width, height float64) *Rect { 
return &Rect{x, y, width, height}
}
```
注意，这里与C/C++不同的是，返回一个局部变量的地址在Go语言中是绝对没有问题的；变量关联的存储在函数返回之后依然存在。
更直接的说，在Go语言中，如果一个局部变量在函数返回后仍然被使用，这个变量会从heap，而不是stack中分配内存。详细参考How do I know whether a variable is allocated on the heap or the stack?。

make
------
内建函数make(T, args)与new(T)的用途不一样。它只用来创建slice，map和channel，并且返回一个初始化的(而不是置零)，类型为T的值（而不是*T）。之所以有所不同，是因为这三个类型的背后引用了使用前必须初始化的数据结构。例如，slice是一个三元描述符，包含一个指向数据（在数组中）的指针，长度，以及容量，在这些项被初始化之前，slice都是nil的。对于slice，map和channel，make初始化这些内部数据结构，并准备好可用的值。

例如，

```go
make([]int, 10, 100)
```
分配一个有100个int的数组，然后创建一个长度为10，容量为100的slice结构，该slice引用包含前10个元素的数组。对应的，new([]int)返回一个指向新分配的，被置零的slice结构体的指针，即指向值为nil的slice的指针。

```go
var p *[]int = new([]int)       // allocates slice structure; *p == nil; rarely useful
var v  []int = make([]int, 100) // the slice v now refers to a new array of 100 ints

// Unnecessarily complex:这种做法实在是很蛋疼
var p *[]int = new([]int)
*p = make([]int, 100, 100)

// Idiomatic:习惯的做法
v := make([]int, 100)
```
记住make只用于map，slice和channel，并且不返回指针。要获得一个显式的指针，使用new进行分配，或者显式地使用一个变量的地址。
