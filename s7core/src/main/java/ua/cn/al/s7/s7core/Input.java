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

import org.apache.commons.math3.FieldElement;


/**
 *
 *  @author Oleksiy Lukin alukin@mail.com
 * @param <T>
 */
public class Input<T extends FieldElement<T>> extends Pin{
    Output connected = null;
    
    public void connect(Output out){
        connected = out;
    }
    
    @Override
 
    @SuppressWarnings("unchecked")
    public T value() {
        return (T) connected.value;
    }
    
}
