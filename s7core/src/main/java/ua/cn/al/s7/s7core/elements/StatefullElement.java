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

import lombok.Getter;
import ua.cn.al.s7.s7core.Element;
import ua.cn.al.s7.s7core.Value;

/**
 *
 * @author Oleksiy Lukin alukin@mail.com
 */
public abstract  class StatefullElement extends Element {
    @Getter
    Value[] state;

    public StatefullElement() {
    }
    
    public StatefullElement(int NInputs, int nOutputs, Value[] initialState) {
        state = new Value[initialState.length];
        for(int i=0;i<initialState.length;i++){
            state[i]=initialState[i].copy();
        }
    } 
    
    public abstract void changeState();

    @Override
    public void tick(){
        changeState();
    }
    
}
