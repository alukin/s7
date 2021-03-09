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

import ua.cn.al.s7.s7core.numbers.Value;

/**
 *
 *  @author Oleksiy Lukin alukin@mail.com
 */
public class Input extends Pin{
    Output conected = null;
    
    public Input(Element ownerElement) {
        super(ownerElement);
    }
    
    public void connect(Output out){
        conected = out;
    }
    
    @Override
    public Value value() {
        return conected.value();
    }

//    @Override
//    public ValueType getValueType() {
//        //TODO: do we have dynamic value types?
//        return ValueType.DOUBLE;
//    }

    
}
