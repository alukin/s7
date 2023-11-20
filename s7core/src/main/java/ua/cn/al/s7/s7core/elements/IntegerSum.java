/*
 * Copyrigh (c) 2021 Oleksiy Lukin
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
public class IntegerSum extends Element{
    private final Long[] weights;
    private final RealInput[] inputs;
    private final RealOutput o;
    public IntegerSum(Long[] weights) {
        this.o = new RealOutput();
        inputs = new RealInput[weights.length];
        addInputs(inputs);
        addOutputs(o);
        this.weights = weights;
        o.value = new Decimal64(0.0D);
    }
    
    @Override
    public void tick() {
        long sum = 0;
        for(int idx=0;idx<weights.length;idx++){
           sum+=weights[idx]*inputs[idx].value().longValue();
        }
        o.value = new Decimal64(sum);
    }
    
}
