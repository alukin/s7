package ua.cn.al.s7.s7kore

abstract class Pin  constructor (val owner: Element){

    abstract fun value(): Double

}