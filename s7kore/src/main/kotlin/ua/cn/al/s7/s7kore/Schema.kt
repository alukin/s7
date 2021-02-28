package ua.cn.al.s7.s7kore

class Schema : Element(0,0){
    val elements = arrayListOf<Element>()
    fun connect(input:Input, output:Output){
        if (!elements.contains(input.owner) ){
            elements.add(input.owner)
        }
        if (!elements.contains(output.owner) ){
            elements.add(output.owner)
        }
        input.connect(output)
    }

    override fun tick() {
        TODO("Not yet implemented")
    }
}