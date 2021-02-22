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

/**
 *
 *  @author Oleksiy Lukin alukin@mail.com
 */
public class Output extends Pin{

    Value value;

    public void setValue(Value value) {
        this.value = value;
    }
    
    public Output(Element ownerElement) {
        super(ownerElement);
    }

    @Override
    public Value value() {
        return value;
    }

    @Override
    public ValueType getValueType() {
       return value.vt;
    }
    
}
