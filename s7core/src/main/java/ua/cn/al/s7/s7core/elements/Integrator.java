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
import ua.cn.al.s7.s7core.numbers.ValueType;

/**
 *
 * @author Oleksiy Lukin alukin@mail.com
 */
public class Integrator extends Element{
    
    public Integrator(double initialValue) {
        super(1,1);
        outputs.get(0).value = new Value(initialValue);
    }
    
    public Integrator() {
        super(1,1);
        outputs.get(0).value = new Value(ValueType.DOUBLE);
    }
     
    @Override
    public void tick() {
       double in =  inputs.get(0).value().getDouble();
       double prev = outputs.get(0).value().getDouble();
       
       double res  = prev+in*getTimeStep();
               
       outputs.get(0).value.set(res);       
    }
    
}
