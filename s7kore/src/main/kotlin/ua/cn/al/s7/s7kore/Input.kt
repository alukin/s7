package ua.cn.al.s7.s7kore

class Input(val owner: Element): Pin(owner) {
    private lateinit var out: Output;

    fun connect(out: Output){
        this.out=out
    }
    override fun value() {
       return out.value();
    }
}