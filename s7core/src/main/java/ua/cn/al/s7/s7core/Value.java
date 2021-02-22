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

/**
 *
 * @author Oleksiy Lukin alukin@mail.com
 */
public class Value{
    ValueType vt;
    Double d;
    Long l;
    BigInteger bi;
    BigDecimal bd;
    
    public Value(double d) {
        this.d = d;
        vt=ValueType.DOUBLE;
    }
    
    public Value(long l) {
        this.l = l;
        vt=ValueType.LONG;
    }

    public Value(BigInteger bi) {
        this.bi = bi;
        vt=ValueType.BIGINT;
    }

    public Value(BigDecimal bd) {
        this.bd = bd;
        vt=ValueType.BIGDEC;
    }
    
    public double getDouble() {
        double res = 0D;
        switch(vt){
            case BIGDEC: res = bd.doubleValue();
            break;
            case BIGINT: res = bi.doubleValue();
            break;
            case DOUBLE: res = d;
            break;
            case LONG: res = l.doubleValue();
        }
        return res;
    }
    
    public long getLong(){
        long res = 0L;
        switch(vt){
            case BIGDEC: res = bd.longValue();
            break;
            case BIGINT: res = bi.longValue();
            break;
            case DOUBLE: res = d.longValue();
            break;
            case LONG: res = l;
        }
        return res;
    }
    
//TODO: getters, setters
    
    @Override
    public String toString() {
        return "" + getDouble();
    }
    
}
