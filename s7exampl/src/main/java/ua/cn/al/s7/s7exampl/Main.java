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
package ua.cn.al.s7.s7exampl;

import ua.cn.al.s7.s7core.Constant;
import ua.cn.al.s7.s7core.Element;
import ua.cn.al.s7.s7core.IntegerSum;
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
       Constant const1 = new Constant(1.0);
       const1.setName("C1");
       Constant const2 = new Constant(1.0); 
       const2.setName("C2");
       Constant const3 = new Constant(1.0);
       const3.setName("C3");
        
       Long[] w = {1L,1L};
       Element isum = new IntegerSum(w);

//       s.insert(const1);
//       s.insert(const2);       
//       s.insert(const3);       
//       s.insert(sum);
//       s.insert(isum);
       
       s.connect(const1.getOut(0), sum.getIn(0));
       s.connect(const2.getOut(0), sum.getIn(1));
       
       s.connect(const3.getOut(0), isum.getIn(0));
       s.connect(sum.getOut(0), isum.getIn(1));
       
       if(s.verify()){
          System.out.println(s.printOrder()); 
          s.runTicks(2);
       }
       System.out.println("Sum R = "+sum.getOut(0).value());
       System.out.println("Sum I = "+isum.getOut(0).value());
    }
    
}
