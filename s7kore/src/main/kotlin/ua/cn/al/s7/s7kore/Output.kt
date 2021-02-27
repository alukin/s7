package ua.cn.al.s7.s7kore

abstract class Output(val owner: Element) : Pin(owner){
    var value: double = 0.0
    set(value) {}

    override fun value(): double {
        return this.value
    }
}