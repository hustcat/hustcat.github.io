---
date: 2014-11-01 18:44:30+00:00
layout: post
title:  "Go语言学习：数组（array）与切片（slice）"
categories: 编程语言
tags: go
excerpt: Go语言中数组与切片的区别
---

数组
-------
数组声明：

```go
ArrayType   = "[" ArrayLength "]" ElementType .
```
例如：

```go
var a [32] int
var b [3][5] int
```

在Go和C中，数组的工作方式有几个重要的差别。在Go中，
（1）数组是值类型。将一个数组赋值给另一个，会拷贝所有的元素。

（2） 如果你给函数传递一个数组，其将收到一个数组的拷贝，而不是它的指针。

（3）数组的大小是其类型的一部分，类型[10]int和[20]int是不同的。数组长度在声明后，就不可更改。

切片
-------
切片声明：

```go
SliceType = "[" "]" ElementType .
```
例如：
var a []int
没有初始化的slice为nil。

切片（slice）对数组进行封装，实际上，切片可以看成大小可以动态变化的数组，这一点类似C++中std::vector。就像std::vector在实际C++编程中大量使用一样，Go中大多数的数组编程都是通过切片完成，而不是简单数组。

一般来说，有两种方式来初始化切片：

（1）通过数组

```go
var myArray [10]int = [10]int{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}
var mySlice []int = myArray[:5]
```

（2）通过make
语法：

```go
make([]T, length, capacity)
```
创建一个初始长度为5，容量为10为切片，切片的每个元素都为0：

```go
slice1 := make([]int, 5, 10)
```

创建一个长度为5的切片，并初始化切片的每个元素：

```go
slice2 := []int{1, 2, 3, 4, 5}
```
对于切片，最重要的特点就是长度是可变的：

```go
slice2 := []int{1, 2, 3, 4, 5}
fmt.Println("slice:", slice2)

slice2 = append(slice2, 6)
fmt.Println("slice:", slice2)
```
输出：

```go
slice: [1 2 3 4 5]
slice: [1 2 3 4 5 6]
```

函数append是Go专门为切片增加元素而提供的一个内建函数。内建的append可以接收任何类型的切片，但是Go语言中却没有C++中的泛型，所以，这需要编译器的支持。简单的讲，这个函数大概如下：

```go
func append(slice []T, elements ...T) []T
```

切片持有对底层数组的引用，如果你将一个切片赋值给另一个，二者都将引用同一个数组。如果函数接受一个切片作为参数，那么其对切片的元素所做的改动，对于调用者是可见的，好比是传递了一个底层数组的指针。

```go
func (f *File) Read(b []byte) (n int, err error)
```
这个os.File的Read方法，它接受一个切片参数，切片中的长度已经设定了要读取的数据的上限。对于C/C++，需要同时提供一个缓冲区的指针，和缓冲区的大小：

```go
int read(File* f, char* buf, int len)
```
从这里可以看到，GO的写法要简单一些。

切片（数组）的遍历
-------
在go语言中，遍历切片或者数组，有两种方式：
传统方式：

```go
for i := 0; i <len(mySlice); i++ { 
fmt.Println("mySlice[", i, "] =", mySlice[i])
}
```
使用range表达式
range表达式有两个返回值，第一个是索引，第二个是元素的值：

```go
for i, v := range mySlice { 
fmt.Println("mySlice[", i, "] =", v)
}
```
使用range让代码更加简洁，所以在go编程中也更加常用。
