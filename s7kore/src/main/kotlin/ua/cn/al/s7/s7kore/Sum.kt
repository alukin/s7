package ua.cn.al.s7.s7kore

class Sum(val weights:Array<Double>) : Element(weights.size,1) {
    override fun tick() {
        var res:Double = 0.0
        for(i in 0..nInputs-1){
            res=res+weights[i]*inputs[i].value()
        }
        outputs[0].value=res
    }
}