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

import ua.cn.al.s7.s7core.Element;
import ua.cn.al.s7.s7core.numbers.Value;

/**
 *
 * @author Oleksiy Lukin alukin@mail.com
 */
public class IntegerSum extends Element{
    private final Long[] weights;

    public IntegerSum(Long[] weights) {
        super(weights.length,1);
        this.weights = weights;
        outputs.get(0).value = new Value(0L);
    }
    
    @Override
    public void tick() {
        Long sum = 0L;
        for(int idx=0;idx<weights.length;idx++){
           Value v =  inputs.get(idx).value();
           long in =  v.getLong();
           sum += weights[idx]*in; 
        }
        outputs.get(0).value.set(sum);
    }
    
}
