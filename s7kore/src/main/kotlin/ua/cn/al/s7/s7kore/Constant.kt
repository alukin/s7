package ua.cn.al.s7.s7kore

class Constant(v:Double):Element(0,1) {
    init{
        outputs[0].value=v
    }
    override fun tick() {
    }
}