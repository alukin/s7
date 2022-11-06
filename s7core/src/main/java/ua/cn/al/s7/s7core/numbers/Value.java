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
package ua.cn.al.s7.s7core.numbers;

import java.math.BigDecimal;
import java.math.BigInteger;
import java.math.MathContext;
import org.apache.commons.math3.Field;
import org.apache.commons.math3.FieldElement;
import org.apache.commons.math3.exception.MathArithmeticException;
import org.apache.commons.math3.exception.NullArgumentException;
import org.apache.commons.math3.util.BigReal;

/**
 *
 * @author Oleksiy Lukin alukin@mail.com
 */
public class Value implements Comparable, FieldElement<Value> {

    static Value ONE = new Value(BigDecimal.ONE);
    static Value ZERO = new Value(BigDecimal.ZERO);
    ValueType vt;
    Object v;

    public Value(ValueType vt) {
        this.vt = vt;
        switch (vt) {
            case BIGDEC:
                v = BigDecimal.ZERO;
                break;
            case BIGINT:
                v = BigInteger.ZERO;
                break;
            case DOUBLE:
                v = 0.0D;
                break;
            case LONG:
                v = 0L;
        }
    }

    public Value(double d) {
        v = d;
        vt = ValueType.DOUBLE;
    }

    public Value(long l) {
        v = l;
        vt = ValueType.LONG;
    }

    public Value(BigInteger bi) {
        v = bi;
        vt = ValueType.BIGINT;
    }

    public Value(BigDecimal bd) {
        v = bd;
        vt = ValueType.BIGDEC;
    }

    public Double getDouble() {
        double res = 0D;
        switch (vt) {
            case BIGDEC:
                res = ((BigDecimal) v).doubleValue();
                break;
            case BIGINT:
                res = ((BigInteger) v).doubleValue();
                break;
            case DOUBLE:
                res = ((Double) v);
                break;
            case LONG:
                res = ((Long) v).doubleValue();
        }
        return res;
    }

    public Long getLong() {
        long res = 0L;
        switch (vt) {
            case BIGDEC:
                res = ((BigDecimal) v).longValue();
                break;
            case BIGINT:
                res = ((BigInteger) v).longValue();
                break;
            case DOUBLE:
                res = ((Double) v).longValue();
                break;
            case LONG:
                res = ((Long) v);
        }
        return res;
    }

    public BigInteger getBigInt() {
        BigInteger res = BigInteger.ZERO;
        switch (vt) {
            case BIGDEC:
                res = ((BigDecimal) v).toBigInteger();
                break;
            case BIGINT:
                res = ((BigInteger) v);
                break;
            case DOUBLE:
                res = BigInteger.valueOf(((Double) v).longValue());
                break;
            case LONG:
                res = BigInteger.valueOf(((Long) v));
        }
        return res;
    }
    
    public BigDecimal getBigDec() {
        BigDecimal res = BigDecimal.ZERO;
        switch (vt) {
            case BIGDEC:
                res = ((BigDecimal) v);
                break;
            case BIGINT:
                res = ((BigDecimal) v);
                break;
            case DOUBLE:
                res = BigDecimal.valueOf(((Double) v).longValue());
                break;
            case LONG:
                res = BigDecimal.valueOf(((Long) v));
        }
        return res;
    }
    
    public void set(Double d) {
        switch (vt) {
            case BIGDEC:
                v = new BigDecimal(d, MathContext.DECIMAL64); 
                break;
            case BIGINT:
                v = BigInteger.valueOf(d.longValue());
                break;
            case DOUBLE:
                v = d;
                break;
            case LONG:
                v = d.longValue();
        }
    }

    public void set(Long l) {
        switch (vt) {
            case BIGDEC:
                v = new BigDecimal(l, MathContext.DECIMAL64); ///.longValue();
                break;
            case BIGINT:
                v = BigInteger.valueOf(l);
                break;
            case DOUBLE:
                v = l.doubleValue();
                break;
            case LONG:
                v = l;
        }

    }

    public void set(Value v) {
        this.vt = v.vt;
        this.v = v.copy().v;
    }

    @Override
    public String toString() {
        String s = "";
        switch (vt) {
            case BIGDEC:
                s = ((BigDecimal) v).toString();
                break;
            case BIGINT:
                s = ((BigInteger) v).toString();
                break;
            case DOUBLE:
                s = ((Double) v).toString();
                break;
            case LONG:
                s = ((Long) v).toString();
        }
        return s;
    }

    @Override
    public int compareTo(Object o) {
        int res = -1;
        if (o instanceof Value) {
            //TODO: better impl
            Value v1 = (Value) o;
            res = this.getDouble().compareTo(v1.getDouble());
        }
        return res;
    }

    public Value copy() {
        Value res = null;
        switch (vt) {
            case BIGDEC:
                res = new Value((BigDecimal) v);
                break;
            case BIGINT:
                res = new Value((BigInteger) v);
                break;
            case DOUBLE:
                res = new Value((Double) v);
                break;
            case LONG:
                res = new Value((Long) v);
        }
        return res;
    }

    public boolean lessThan(Value value) {
        //TODO: better impl
        return this.getDouble() < value.getDouble();
    }

    @Override
    public Value subtract(Value a) throws NullArgumentException {

        switch (vt) {
            case BIGDEC:
                v = BigDecimal.ZERO;
                break;
            case BIGINT:
                v = BigInteger.ZERO;
                break;
            case DOUBLE:
                v = 0.0D;
                break;
            case LONG:
                v = 0L;
        }
        return null;
    }

    @Override
    public Value negate() {
        throw new UnsupportedOperationException("Not supported yet."); 
    }

    @Override
    public Value multiply(int n) {
        throw new UnsupportedOperationException("Not supported yet."); //To change body of generated methods, choose Tools | Templates.
    }

    @Override
    public Value multiply(Value a) throws NullArgumentException {
        throw new UnsupportedOperationException("Not supported yet."); //To change body of generated methods, choose Tools | Templates.
    }

    @Override
    public Value divide(Value a) throws NullArgumentException, MathArithmeticException {
        BigReal br = BigReal.ONE;
        throw new UnsupportedOperationException("Not supported yet."); //To change body of generated methods, choose Tools | Templates.
    }

    @Override
    public Value reciprocal() throws MathArithmeticException {
        throw new UnsupportedOperationException("Not supported yet."); //To change body of generated methods, choose Tools | Templates.
    }

    @Override
    public Field<Value> getField() {
        throw new UnsupportedOperationException("Not supported yet."); //To change body of generated methods, choose Tools | Templates.
    }

    @Override
    public Value add(Value a) throws NullArgumentException {
        throw new UnsupportedOperationException("Not supported yet."); //To change body of generated methods, choose Tools | Templates.
    }
}
