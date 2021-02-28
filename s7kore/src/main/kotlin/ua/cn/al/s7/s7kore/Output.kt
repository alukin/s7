package ua.cn.al.s7.s7kore

class Output(owner: Element) : Pin(owner){
    var value: Double = 0.0

    override fun value(): Double {
        return value
    }
}