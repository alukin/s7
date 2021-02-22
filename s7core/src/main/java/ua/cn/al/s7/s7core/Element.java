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

/**
 *
 * @author Oleksiy Lukin alukin@mail.com

 */
public abstract class Element {
    List<Input> inputs = new ArrayList<>();
    List<Output> outputs = new ArrayList<>();
    Value[] state;

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
        return inputs.get(idx);
    }
    
    public Output getOut(int idx){
        return outputs.get(idx);
    }
}
