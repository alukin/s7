package ua.cn.al.s7.s7kore

abstract class Element constructor(val nInputs: Int, val nOutputs: Int) {
    var inputs = arrayListOf<Input>()
    var outputs = arrayListOf<Output>()
    lateinit var ownerSchema:Schema

    init {
        for (i in 1..nInputs) {
            inputs.add(Input(this))
        }
        for (i in 1..nOutputs) {
            outputs.add(Output(this))
        }
    }
    fun getOutput(i:Int):Output{
        return outputs[i]
    }
    fun getInput(i:Int):Input{
        return inputs[i]
    }

    abstract fun tick()

}