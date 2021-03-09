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
 *  Output pin has state in the "value" variable.
 *  Actually, if element has output pinsm it has states
 *  saved from previous "tick". It could be used or ignored in the
 *  Element's tick() method.
 * 
 *  @author Oleksiy Lukin alukin@mail.com
 */
public class Output extends Pin{

    public Value value;
    
    public Output(Element ownerElement) {
        super(ownerElement);
    }

    @Override
    public Value value() {
        return value;
    }

    
}
