package ua.cn.al.s7.s7kore

import java.util.ArrayList

class Schema : Element(0,0){
    var currentTime = 0.0
    var timeStep=1E-9

    val elements: ArrayList<Element> = arrayListOf()

    fun connect(input:Input, output:Output){
        if (!elements.contains(input.owner) ){
            elements.add(input.owner)
            input.owner.ownerSchema=this
        }
        if (!elements.contains(output.owner) ){
            elements.add(output.owner)
            input.owner.ownerSchema=this
        }
        input.connect(output)
    }

    override fun tick() {
        for(e in elements){
            e.tick()
            currentTime+=timeStep
        }
    }
    fun runTicks(n:Int){
        for(i in 0..n-1){
            tick()
        }
    }
}