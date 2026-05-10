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

import org.apache.commons.math3.util.Decimal64;
import ua.cn.al.s7.s7core.Element;
import ua.cn.al.s7.s7core.RealInput;
import ua.cn.al.s7.s7core.RealOutput;


/**
 *
 * @author Oleksiy Lukin alukin@mail.com
 */
public class Comparator extends Element{
    
    private final Decimal64 low;
    private final Decimal64 high;
    private final RealInput i1 = new RealInput();
    private final RealInput i2  = new RealInput();
    private final RealOutput o = new RealOutput();
    public Comparator(double low, double high) {

        addInputs(i1,i2);
        addOutputs(o);
        
        this.high = new Decimal64(high);
        this.low = new Decimal64(low);
    }

    @Override
    public void tick() {
        double v1 = i1.value().doubleValue();
        double v2 = i2.value().doubleValue();
        if (v1 < v2){
            o.value = low;
        } else{
            o.value = high;
        }
    }
    
}
