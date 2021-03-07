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
package ua.cn.al.s7.s7core.registrator;

import java.util.ArrayList;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import ua.cn.al.s7.s7core.Element;

/**
 *
 * @author Oleksiy Lukin alukin@mail.com
 */
@Slf4j
public class InMemoryReg extends Element{
    @Getter
    private final int maxRecords;
    @Getter
    private int index=0;
    @Getter
    private int nChannels;
    @Getter
    public ArrayList<double[]> store = new ArrayList<>();
    
    public InMemoryReg(int nChannels, int maxRecords) {
        super(nChannels,0);
        this.maxRecords=maxRecords;
        this.nChannels=nChannels;
        for(int i=0;i<nChannels;i++){
            store.add(new double[maxRecords]);
        }
    }

    @Override
    public void tick() {
        for(int i=0;i<nChannels;i++){
            store.get(i)[index]=inputs.get(i).value().getDouble();
        }
        index++;
        index=index % maxRecords;
        if(index==0){
           log.error("Max capacity {} reached, overwriting earlier values",maxRecords);
        }
    }
    
}
