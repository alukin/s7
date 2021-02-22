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
package ua.cn.al.s7.example;

import ua.cn.al.s7.s7core.Constant;
import ua.cn.al.s7.s7core.Element;
import ua.cn.al.s7.s7core.Schema;
import ua.cn.al.s7.s7core.RealSum;

/**
 *
 * @author Oleksiy Lukin alukin@mail.com
 */
public class Main {

    /**
     * @param args the command line arguments
     */
    public static void main(String[] args) {
       Schema s = new Schema();
       Double[] weights = {1.0,1.0};
       Element sum = new RealSum(weights);
       Constant cons1 = new Constant(1.0);
       Constant cons2 = new Constant(1.0);       
 
       s.insert(sum);
       s.insert(cons1);
       s.insert(cons2);
       
       s.connect( cons1.getOut(0), sum.getIn(0));
       s.connect( cons2.getOut(0), sum.getIn(1));
       
       if(s.verify()){
          s.runTicks(2);
       }
       System.out.println("Sum="+sum.getOut(0).value());
    }
    
}
