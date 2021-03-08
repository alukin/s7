/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * Copyrigh (c) 2021 Oleksiy Lukin
 */
package ua.cn.al.s7.s7core;

import java.util.ArrayList;
import java.util.List;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

/**
 *
 * @author Oleksiy Lukin alukin@mail.com
 */
@Slf4j
public abstract class Element {
    @Getter
    @Setter
    protected String name = this.getClass().getSimpleName();    
    @Getter
    public List<Input> inputs = new ArrayList<>();
    @Getter
    public List<Output> outputs = new ArrayList<>();
    @Getter
    @Setter        
    protected Schema ownerSchema;
    
    public Element() {
    }
    
    public Element(int nInputs, int nOutputs) {
        for(int i = 0; i<nInputs; i++ ){
            Input in = new Input(this);
            in.setName(""+i);
            inputs.add(in);
        }
        for(int i = 0; i<nOutputs; i++ ){
            Output out = new Output(this);
            out.setName(""+i);
            outputs.add(out);
        }        
    }    
    
    public abstract void tick();
    
    public Input in(int idx){
        int idx1=idx+1;
        int sz = inputs.size();
        if(idx1>sz || idx1<1){
            log.trace("Idex of input is out of the range");
            return null;
        }
        return inputs.get(idx);
    }
    
    public Output out(int idx){
        int idx1=idx+1;
        int sz = outputs.size();
        if(idx1>sz || idx1<1){
            log.trace("Idex of output is out of the range");
            return null;
        }
        return outputs.get(idx);
    }
    
    public boolean isGenerator(){
        boolean res = inputs.isEmpty();
        return res;
    }
    
    public Double getTime(){
        return ownerSchema.schemaTime;        
    }
    
    public Double getTimeStep(){
        return ownerSchema.timeStep;
    }
}
