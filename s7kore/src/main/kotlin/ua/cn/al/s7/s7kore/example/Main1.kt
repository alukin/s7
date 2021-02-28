package ua.cn.al.s7.s7kore.example

import ua.cn.al.s7.s7kore.Constant
import ua.cn.al.s7.s7kore.Integrator
import ua.cn.al.s7.s7kore.Schema
import ua.cn.al.s7.s7kore.Sum

fun main(){
   println("S7 kore example 1")
   val s = Schema()
   val c1 = Constant (1.0)
   val c2 = Constant(2.0)
   val sum = Sum(arrayOf(1.0,1.0))
   val integral = Integrator()
   s.connect(sum.getInput(0),c1.getOutput(0))
   s.connect(sum.getInput(1),c2.getOutput(0))
   s.connect(integral.getInput(0),c1.getOutput(0))
   s.runTicks(100)

   val res = sum.getOutput(0).value()
   println("Sum=${res} Integral = ${integral.getOutput(0).value}")

}