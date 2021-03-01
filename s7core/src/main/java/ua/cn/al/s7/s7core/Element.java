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
    public List<Input> inputs = new ArrayList<>();
    public List<Output> outputs = new ArrayList<>();
    Input dummyIn = new Input(this);
    Output dummyOut = new Output(this);
    @Getter
    @Setter        
    Schema ownerSchema;
    
    public Element() {
    }
    
    public Element(int nInputs, int nOutputs) {
        for(int i = 0; i<nInputs; i++ ){
            inputs.add(new Input(this));
        }
        for(int i = 0; i<nOutputs; i++ ){
            outputs.add(new Output(this));
        }        
    }    
    
    public abstract void tick();
    
    public Input getIn(int idx){
        int idx1=idx+1;
        int sz = inputs.size();
        if(idx1>sz || idx1<1){
            log.warn("Idex of input is out of the range");
            return dummyIn;
        }
        return inputs.get(idx);
    }
    
    public Output getOut(int idx){
        int idx1=idx+1;
        int sz = outputs.size();
        if(idx1>sz || idx1<1){
            log.warn("Idex of output is out of the range");
            return dummyOut;
        }
        return outputs.get(idx);
    }
    
    public boolean isGenerator(){
        boolean res = inputs.isEmpty();
        return res;
    }
    public Double getTime(){
        return ownerSchema.shemaTime;        
    }
    public Double getTimeStep(){
        return ownerSchema.timeStep;
    }
}
