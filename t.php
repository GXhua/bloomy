<?php

//参数1 预计要存多少个元素进去（2kw），参数2 期望的"假in"错误率（10万分之一）
$bf = new BloomFilter(20000000, 0.000001);
//$bfhour = new BloomFilter(10000000, 0.000001);
//sleep(1000);

$start = microtime(true) * 1000;
$i = 20000000;
//$i = 20000000;
while (--$i) {
    $bf->add("foo" . $i);
}
$end = microtime(true) * 1000;
var_dump($end - $start);
var_dump($bf->has("foo")); // must have it
var_dump($bf->has("foo232323")); // must have it
var_dump($bf->has("foo12121212")); // must have it


file_put_contents("/tmp/bloom",serialize($bf));//2kw  150ms左右
$end2 = microtime(true) * 1000;
var_dump($end2 - $end);



$bf2 = unserialize(file_get_contents("/tmp/bloom"));//2kw 80ms左右
var_dump($bf2->has("foo")); // must have it
var_dump($bf2->has("foo232323")); // must have it
var_dump($bf2->has("foo12121212")); // must have it
$end3 = microtime(true) * 1000;
var_dump($end3 - $end2);
//array(5) {
//  ["error_rate"]=>
//  float(0.0001)
//  ["num_hashes"]=> 每个值使用几个hash函数
//  int(13)
//  ["filter_size"]=> 有多少个bit域
//  int(383459096)
//  ["filter_size_in_bytes"]=> 占用的字节数
//  int(47932387)
//  ["num_items"]=> 存了几个元素
//  int(1)
//}
var_dump($bf->getInfo());

sleep(1000);
