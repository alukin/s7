package ua.cn.al.s7.s7kore

class Integrator(): Element(1,1) {
    override fun tick() {
        outputs[0].value += inputs[0].value()*ownerSchema.timeStep
    }

}