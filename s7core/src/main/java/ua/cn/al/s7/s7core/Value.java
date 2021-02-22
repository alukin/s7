/*
 * Copyright (C) 2021 Oleksiy Lukin
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
package ua.cn.al.s7.s7core;

import java.math.BigDecimal;
import java.math.BigInteger;
import java.math.MathContext;

/**
 *
 * @author Oleksiy Lukin alukin@mail.com
 */
public class Value{
    ValueType vt;
    Object v;
    
    public Value(ValueType vt){
        this.vt=vt;
    }
    
    public Value(double d) {
        v = d;
        vt=ValueType.DOUBLE;
    }
    
    public Value(long l) {
        v = l;
        vt=ValueType.LONG;
    }

    public Value(BigInteger bi) {
        v = bi;
        vt=ValueType.BIGINT;
    }

    public Value(BigDecimal bd) {
        v = bd;
        vt=ValueType.BIGDEC;
    }
    
    public double getDouble() {
        double res = 0D;
        switch(vt){
            case BIGDEC: res = ((BigDecimal)v).doubleValue();
            break;
            case BIGINT: res = ((BigInteger)v).doubleValue();
            break;
            case DOUBLE: res = ((Double)v);
            break;
            case LONG: res = ((Long)v).doubleValue();
        }
        return res;
    }
    
    public long getLong(){
        long res = 0L;
        switch(vt){
            case BIGDEC: res = ((BigDecimal)v).longValue();
            break;
            case BIGINT: res = ((BigInteger)v).longValue();
            break;
            case DOUBLE: res = ((Double)v).longValue();
            break;
            case LONG: res = ((Long)v);
        }
        return res;
    }
    
    public void set(Double d){
          switch(vt){
            case BIGDEC: v = new BigDecimal(d, MathContext.DECIMAL64); ///.longValue();
            break;
            case BIGINT: v = BigInteger.valueOf(d.longValue());
            break;
            case DOUBLE: v = d;
            break;
            case LONG: v = d.longValue();
        }      
    }
    
    public void set(Long l){
          switch(vt){
            case BIGDEC: v = new BigDecimal(l, MathContext.DECIMAL64); ///.longValue();
            break;
            case BIGINT: v = BigInteger.valueOf(l);
            break;
            case DOUBLE: v = l.doubleValue();
            break;
            case LONG: v = l;
        }        
        
    }
//TODO: getters, setters
    
    @Override
    public String toString() {
        return "" + getDouble();
    }
    
}
