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

import java.io.IOException;
import java.util.logging.Level;
import java.util.logging.Logger;
import ua.cn.al.s7.s7core.elements.Constant;
import ua.cn.al.s7.s7core.Element;
import ua.cn.al.s7.s7core.Schema;
import ua.cn.al.s7.s7core.elements.RealSum;
import ua.cn.al.s7.s7core.elements.Integrator;
import ua.cn.al.s7.s7core.registrator.TextFileReg;
/**
 *
 * @author Oleksiy Lukin alukin@mail.com
 */

public class IntergatorsSinus {

    /**
     * @param args the command line arguments
     */
    public static void main(String[] args) {
       Schema s = new Schema( 0.01D );
       Double runTime = 10.0; //10s
       long nTicks = s.getTicks(runTime);
       
       Double[] weights = {1.0,-1.0};
       Element sum = new RealSum(weights);
       Element const1 = new Constant(0.0);
       const1.setName("C1");

       Element integrator1 = new Integrator(0.000001D);
       Element integrator2 = new Integrator(0.0);
       TextFileReg reg = null; 
        try {
            reg = new TextFileReg(1, System.out);
        } catch (IOException ex) {
            Logger.getLogger(IntergatorsSinus.class.getName()).log(Level.SEVERE, null, ex);
            System.exit(1);
        }
       
       s.connect(const1.out(0), sum.in(0)); 
       s.connect(sum.out(0), integrator1.in(0));
       s.connect(integrator1.out(0), integrator2.in(0));
       s.connect(integrator2.out(0), sum.in(1));
       s.connect(integrator2.out(0), reg.in(0));
       
       if(s.verify()){
          System.out.println(s.printOrder()); 
          s.runTicks(nTicks);
       }
       
        try {
            reg.close();
        } catch (IOException ex) {
            Logger.getLogger(IntergatorsSinus.class.getName()).log(Level.SEVERE, null, ex);
        }
       
       System.out.println("Sum R = "+sum.out(0).value());
       System.out.println("Integrator2 = "+integrator2.out(0).value());
    }
    
}
