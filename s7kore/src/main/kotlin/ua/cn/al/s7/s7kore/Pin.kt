package ua.cn.al.s7.s7kore

abstract class Pin public constructor (val owner: Element){

    abstract open fun value(): Double

}