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
 *  Output pin has state in the "value" variable.
 *  Actually, if element has output pinsm it has states
 *  saved from previous "tick". It could be used or ignored in the
 *  Element's tick() method.
 * 
 *  @author Oleksiy Lukin alukin@mail.com
 *  @param <T>
 */
public class Output<T extends FieldElement<T>> extends Pin{

    public T value;

    @Override
    public T value() {
        return value;
    }

}
