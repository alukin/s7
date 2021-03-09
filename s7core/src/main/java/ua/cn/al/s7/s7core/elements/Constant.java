/*
 * Copyright (C) 2021 Oleksiy Lukin alukin@mail.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
package ua.cn.al.s7.s7core.elements;

import ua.cn.al.s7.s7core.Element;
import ua.cn.al.s7.s7core.numbers.Value;

/**
 *
 * @author Oleksiy Lukin alukin@mail.com
 */
public class Constant extends Element{

    public Constant(Double value) {
       super(0,1); 
       outputs.get(0).value = new Value(value);
       name="Const="+value.toString();
    }

    @Override
    public void tick() {
    }
    
}
